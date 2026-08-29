#include "span_scan.h"
#include <cstdint>

namespace ovb {

// Byte-identical to the body of the `for (int y = rowY0; y < rowY1; y++)`
// loop in SleepAnimation::publishOverlayRanges (the `sy >= 0 && sy < h`
// branch), with the surrounding range/offset bookkeeping stripped out: this
// function is exactly the per-row work, called once per row by the caller.
int scanRow_ref(const uint8_t *__restrict rowAlpha3, int panelW, uint32_t *__restrict rowRuns,
                uint8_t *__restrict cellRow) {
    const uint8_t *a = rowAlpha3;
    int nRuns = 0;
    int runStart = -1;
    for (int x = 0; x < panelW; x++, a += 3) {
        if (*a != 0) {
            if (runStart < 0) {
                runStart = x;
            }
            if (cellRow != nullptr) {
                uint8_t &c = cellRow[x >> SCRIM_SHIFT];
                if (*a > c) {
                    c = *a;
                }
            }
            continue;
        }
        if (runStart < 0) {
            continue;
        }
        nRuns = emitRun_ref(rowRuns, nRuns, runStart, x, RUN_GAP_MERGE);
        runStart = -1;
    }
    if (runStart >= 0) {
        nRuns = emitRun_ref(rowRuns, nRuns, runStart, panelW, RUN_GAP_MERGE);
    }
    return nRuns;
}

// ---------------------------------------------------------------------------
// Shared per-pixel body, factored out so every variant below that falls back
// to a per-pixel scan uses *exactly* this code, not a parallel reimplementing
// of it -- that is what makes their bit-exactness against scanRow_ref a
// structural property instead of something to hope for after the fact.
// Byte-identical to the loop body in scanRow_ref above, except:
//   - HAS_CELL replaces the `cellRow != nullptr` runtime check. In the real
//     caller (publishOverlayRanges) doScrim -- and so whether cellRow is
//     null -- is a per-publish constant: every row of one publish takes the
//     same branch, never a mix. scanRow_ref re-tests it on every covered
//     pixel anyway, because a plain runtime pointer gives the compiler
//     nothing to hoist. Making it a template bool lets the caller test it
//     once (see scanRow_spec) instead of once per covered pixel, and the
//     `if (HAS_CELL)` compiles away entirely in the false instantiation.
//   - the caller passes an [x0, x1) sub-range and the running (nRuns,
//     runStart) state by reference, so the block-skip variants below can
//     call this for one block at a time and pick up exactly where the
//     previous block (or the fast-empty path) left off.
template <bool HAS_CELL>
__attribute__((always_inline)) inline void scanSpanScalar(const uint8_t *__restrict a, int x0, int x1,
                                                           uint32_t *__restrict rowRuns, int &nRuns, int &runStart,
                                                           uint8_t *__restrict cellRow) {
    for (int x = x0; x < x1; x++, a += 3) {
        if (*a != 0) {
            if (runStart < 0) {
                runStart = x;
            }
            if (HAS_CELL) {
                uint8_t &c = cellRow[x >> SCRIM_SHIFT];
                if (*a > c) {
                    c = *a;
                }
            }
            continue;
        }
        if (runStart < 0) {
            continue;
        }
        nRuns = emitRun_ref(rowRuns, nRuns, runStart, x, RUN_GAP_MERGE);
        runStart = -1;
    }
}

template <bool HAS_CELL>
static int scanRowSpecT(const uint8_t *__restrict rowAlpha3, int panelW, uint32_t *__restrict rowRuns,
                        uint8_t *__restrict cellRow) {
    int nRuns = 0;
    int runStart = -1;
    scanSpanScalar<HAS_CELL>(rowAlpha3, 0, panelW, rowRuns, nRuns, runStart, cellRow);
    if (runStart >= 0) {
        nRuns = emitRun_ref(rowRuns, nRuns, runStart, panelW, RUN_GAP_MERGE);
    }
    return nRuns;
}

// scanRow_ref with the cellRow-null check hoisted out of the loop (see the
// comment on scanSpanScalar above). No other behavioural change: still one
// full per-pixel scan, no block skipping.
int scanRow_spec(const uint8_t *__restrict rowAlpha3, int panelW, uint32_t *__restrict rowRuns,
                 uint8_t *__restrict cellRow) {
    return cellRow != nullptr ? scanRowSpecT<true>(rowAlpha3, panelW, rowRuns, cellRow)
                              : scanRowSpecT<false>(rowAlpha3, panelW, rowRuns, cellRow);
}

// ---------------------------------------------------------------------------
// Block-skip correctness note (applies to scanRowBlockT, scanRow_pie_model
// and scanRow_pie_asm below -- all three follow this same shape).
//
// scanRow_ref processes pixels strictly left to right, and its behaviour at
// pixel x depends only on two things: *a (the byte at x) and whether
// runStart is currently open. That is a purely local, causal state machine,
// so splitting the row into consecutive [x0, x1) blocks and reproducing
// exactly the same per-byte decisions -- by whatever means, as long as the
// (nRuns, runStart) state carried into a block is correct -- reproduces
// exactly the same final (nRuns, rowRuns, cellRow), one block at a time.
//
// Two block outcomes:
//   - "empty" (every alpha byte in [x0, x1) is zero): nothing in the block
//     can start a run (nothing is nonzero) and nothing can update cellRow
//     (same reason). If a run was open coming in, scanRow_ref would hit the
//     first zero byte at x0 -- the block is entirely zero, so x0 is that
//     first zero -- and close it there. So: close any open run at x0, touch
//     nothing else, and move on. This is the ONLY place a block-skip variant
//     calls emitRun_ref outside of scanSpanScalar, and it passes exactly the
//     (runStart, x0) pair scanRow_ref's own loop would have passed at that
//     same transition.
//   - "nonzero": at least one byte in the block is nonzero, and which ones
//     is not known without looking, so fall back to scanSpanScalar over
//     exactly this block's [x0, x1) -- literally scanRow_ref's inner loop,
//     restricted to this range, carrying the same (nRuns, runStart) state a
//     continuous scan would have at x0.
//
// Since every call scanRow_ref would have made to emitRun_ref is reproduced,
// in the same order, with the same arguments, by one or the other of these
// two paths, the two are bit-identical for any input -- including the
// RUNS_PER_ROW overflow path (n >= RUNS_PER_ROW forces a merge inside
// emitRun_ref itself, unmodified and shared by every variant here, so
// overflow behaviour does not need separate reasoning per variant) and a
// fully-empty or fully-opaque row (every block empty, or the single loop
// through scanSpanScalar behaves exactly as scanRow_ref's own loop would).
//
// The empty-block probe itself is a branch-free OR-accumulate with a fixed
// trip count for every full block (only a ragged final block, when BLOCK
// does not evenly divide panelW, has a runtime-variable count) -- a
// hardware zero-overhead LOOP candidate where scanRow_ref's per-pixel body,
// per BASELINE-OVERLAY.md, is not.
template <int BLOCK, bool HAS_CELL>
static int scanRowBlockT(const uint8_t *__restrict rowAlpha3, int panelW, uint32_t *__restrict rowRuns,
                         uint8_t *__restrict cellRow) {
    int nRuns = 0;
    int runStart = -1;
    int x0 = 0;
    while (x0 < panelW) {
        int x1 = x0 + BLOCK;
        if (x1 > panelW) {
            x1 = panelW;
        }
        uint32_t orAcc = 0;
        {
            const uint8_t *ap = rowAlpha3 + static_cast<size_t>(x0) * 3;
            for (int x = x0; x < x1; x++, ap += 3) {
                orAcc |= *ap;
            }
        }
        if (orAcc == 0) {
            if (runStart >= 0) {
                nRuns = emitRun_ref(rowRuns, nRuns, runStart, x0, RUN_GAP_MERGE);
                runStart = -1;
            }
        } else {
            scanSpanScalar<HAS_CELL>(rowAlpha3 + static_cast<size_t>(x0) * 3, x0, x1, rowRuns, nRuns, runStart,
                                     cellRow);
        }
        x0 = x1;
    }
    if (runStart >= 0) {
        nRuns = emitRun_ref(rowRuns, nRuns, runStart, panelW, RUN_GAP_MERGE);
    }
    return nRuns;
}

template <int BLOCK>
static int scanRowBlockDispatch(const uint8_t *__restrict rowAlpha3, int panelW, uint32_t *__restrict rowRuns,
                                uint8_t *__restrict cellRow) {
    return cellRow != nullptr ? scanRowBlockT<BLOCK, true>(rowAlpha3, panelW, rowRuns, cellRow)
                              : scanRowBlockT<BLOCK, false>(rowAlpha3, panelW, rowRuns, cellRow);
}

// BLOCK=16 and BLOCK=32 pixels. A quick host-side census of the synthetic
// overlay (not committed, see the report) found ~64% of 16px blocks and
// ~60% of 32px blocks entirely empty, against only ~16% of whole 480px
// rows -- most of a "non-empty" row is still empty in patches (the gaps
// between a dial's rim, icons, and lines of text), which is what makes a
// sub-row block probe worth more here than a whole-row one, per
// BASELINE-OVERLAY.md's suggestion.
int scanRow_block16(const uint8_t *__restrict rowAlpha3, int panelW, uint32_t *__restrict rowRuns,
                    uint8_t *__restrict cellRow) {
    return scanRowBlockDispatch<16>(rowAlpha3, panelW, rowRuns, cellRow);
}
int scanRow_block32(const uint8_t *__restrict rowAlpha3, int panelW, uint32_t *__restrict rowRuns,
                    uint8_t *__restrict cellRow) {
    return scanRowBlockDispatch<32>(rowAlpha3, panelW, rowRuns, cellRow);
}

// ---------------------------------------------------------------------------
// PIE model: a byte-exact, host-buildable description of the block-empty
// probe scanRow_pie_asm computes on the vector unit. See the block comment
// above pieBlockNonzero16 in the __XTENSA__ section below for the derivation
// of the lane layout; the three masks here are that same layout transcribed
// as plain byte tables instead of the assembly's kSpanPieMasks constant (that
// constant lives inside the __XTENSA__ guard, so this model reproduces it
// rather than sharing it, on purpose -- sharing it would mean a bug in the
// mask table could not be caught by comparing the two independently).
//
// One 16-pixel (48-byte) group is exactly three 128-bit lanes (16 bytes
// each): lane 0 = block bytes [0,16), lane 1 = [16,32), lane 2 = [32,48).
// `block` here, like rowAlpha3 itself, points AT pixel x0's alpha byte, not
// at its colour0 byte -- consecutive pixels' alpha bytes are the ones 3
// apart, at *relative* offset 3p for pixel p (not 3p+2; that offset would
// be right only if `block` pointed at a pixel's first byte, which it does
// not). For p in [0,16): offsets 0,3,6,9,12,15 land in lane 0 (p=0..5, six
// of them -- one more than the other two lanes get), 2,5,8,11,14 in lane 1
// (p=6..10, offset-16), and 1,4,7,10,13 in lane 2 (p=11..15, offset-32). A
// real vector probe ANDs each lane against a mask that is 0xFF at those
// offsets and 0x00 elsewhere, ORs the three masked lanes together, and
// tests the reduction for zero; this model does the same masking and
// OR-reduction with a byte loop instead of EE.ANDQ/EE.ORQ, since those
// instructions do not exist on this host.
static bool pieBlockNonzero16Model(const uint8_t *__restrict block) {
    static constexpr uint8_t kMask0[16] = {0xFF, 0, 0, 0xFF, 0, 0, 0xFF, 0, 0, 0xFF, 0, 0, 0xFF, 0, 0, 0xFF};
    static constexpr uint8_t kMask1[16] = {0, 0, 0xFF, 0, 0, 0xFF, 0, 0, 0xFF, 0, 0, 0xFF, 0, 0, 0xFF, 0};
    static constexpr uint8_t kMask2[16] = {0, 0xFF, 0, 0, 0xFF, 0, 0, 0xFF, 0, 0, 0xFF, 0, 0, 0xFF, 0, 0};
    uint8_t acc = 0;
    for (int i = 0; i < 16; i++) {
        acc = static_cast<uint8_t>(acc | (block[i] & kMask0[i]) | (block[16 + i] & kMask1[i]) |
                                   (block[32 + i] & kMask2[i]));
    }
    return acc != 0;
}

template <bool HAS_CELL>
static int scanRowPieModelT(const uint8_t *__restrict rowAlpha3, int panelW, uint32_t *__restrict rowRuns,
                            uint8_t *__restrict cellRow) {
    constexpr int BLOCK = 16;
    int nRuns = 0;
    int runStart = -1;
    int x0 = 0;
    for (; x0 + BLOCK <= panelW; x0 += BLOCK) {
        if (!pieBlockNonzero16Model(rowAlpha3 + static_cast<size_t>(x0) * 3)) {
            if (runStart >= 0) {
                nRuns = emitRun_ref(rowRuns, nRuns, runStart, x0, RUN_GAP_MERGE);
                runStart = -1;
            }
            continue;
        }
        scanSpanScalar<HAS_CELL>(rowAlpha3 + static_cast<size_t>(x0) * 3, x0, x0 + BLOCK, rowRuns, nRuns, runStart,
                                 cellRow);
    }
    if (x0 < panelW) { // ragged tail shorter than one 16px probe block
        scanSpanScalar<HAS_CELL>(rowAlpha3 + static_cast<size_t>(x0) * 3, x0, panelW, rowRuns, nRuns, runStart,
                                  cellRow);
    }
    if (runStart >= 0) {
        nRuns = emitRun_ref(rowRuns, nRuns, runStart, panelW, RUN_GAP_MERGE);
    }
    return nRuns;
}

// Registered variant: the bit-exact, host-testable stand-in for
// scanRow_pie_asm's arithmetic. See the ScanRowVariant table below --
// bitExactRequired is true for this one (unlike the real asm), precisely
// because it is plain C++ and this harness can run it.
int scanRow_pie_model(const uint8_t *__restrict rowAlpha3, int panelW, uint32_t *__restrict rowRuns,
                      uint8_t *__restrict cellRow) {
    return cellRow != nullptr ? scanRowPieModelT<true>(rowAlpha3, panelW, rowRuns, cellRow)
                              : scanRowPieModelT<false>(rowAlpha3, panelW, rowRuns, cellRow);
}

#if defined(__XTENSA__)
// ---------------------------------------------------------------------------
// Real ESP32-S3 PIE (vector, "EE.*") implementation of the probe
// scanRow_pie_model describes above. Xtensa-only inline asm: cannot build,
// and has never been run, on this x86 host -- see piePending on this
// variant's kScanRowVariants entry. Mirrors the register/constant-hoisting
// style of scale565Oct in SleepAnimation.cpp (masks loaded once into fixed q
// registers, SAR/loop state held across the whole call) rather than
// introducing a new idiom.
//
// 0xFF where a load's byte holds an alpha value, 0x00 elsewhere -- the same
// lane layout scanRow_pie_model's kMask0/1/2 describe, here as the literal
// 48-byte table EE.VLD.128 reads three lanes out of.
alignas(16) static const uint8_t kSpanPieMasks[48] = {
    0xFF, 0,    0,    0xFF, 0,    0,    0xFF, 0,    0,    0xFF, 0,    0,    0xFF, 0,    0,    0xFF,
    0,    0,    0xFF, 0,    0,    0xFF, 0,    0,    0xFF, 0,    0,    0xFF, 0,    0,    0xFF, 0,
    0,    0xFF, 0,    0,    0xFF, 0,    0,    0xFF, 0,    0,    0xFF, 0,    0,    0xFF, 0,    0,
};

// True if any alpha byte in the 48-byte (16-pixel) block at `block` is
// nonzero. `block` must be 16-byte aligned: EE.VLD.128 forces the low four
// address bits to zero rather than trapping (the same hazard scale565Oct's
// comment documents), so an unaligned pointer here would silently read the
// wrong 16 bytes instead of failing -- scanRow_pie_asm below only calls this
// after confirming alignment, and falls back to pure scalar otherwise.
__attribute__((noinline)) static bool pieBlockNonzero16(const uint8_t *__restrict block) {
    alignas(16) uint32_t reduce[4];
    const uint8_t *rd = block;
    const uint8_t *masks = kSpanPieMasks;
    uint32_t *wr = reduce;
    asm volatile("ee.vld.128.ip q3, %[m], 16\n" // q3 = lane-0 alpha mask
                 "ee.vld.128.ip q4, %[m], 16\n" // q4 = lane-1 alpha mask
                 "ee.vld.128.ip q5, %[m], 16\n" // q5 = lane-2 alpha mask
                 "ee.vld.128.ip q0, %[rd], 16\n" // block bytes [0,16)
                 "ee.vld.128.ip q1, %[rd], 16\n" // block bytes [16,32)
                 "ee.vld.128.ip q2, %[rd], 16\n" // block bytes [32,48)
                 "ee.andq q0, q0, q3\n"          // keep only the alpha lanes
                 "ee.andq q1, q1, q4\n"
                 "ee.andq q2, q2, q5\n"
                 "ee.orq q0, q0, q1\n"
                 "ee.orq q0, q0, q2\n"
                 "ee.vst.128.ip q0, %[wr], 16\n" // spill so scalar code can reduce it
                 : [rd] "+r"(rd), [m] "+r"(masks), [wr] "+r"(wr)
                 :
                 : "memory");
    return (reduce[0] | reduce[1] | reduce[2] | reduce[3]) != 0;
}

// scanRow_pie_model with the block probe replaced by the real vector
// instruction sequence above. Only takes the vector path for a row whose
// pointer is 16-byte aligned and whose width is an exact multiple of the
// 16px probe block; anything else -- a ragged tail, or a caller whose xoff
// leaves rowAlpha3 unaligned, which OVERLAY_EXT_MARGIN*3+2 does not
// guarantee for an arbitrary xoff -- falls back to scanRow_block16 in full,
// which is proven bit-exact against scanRow_ref above. So this can never be
// less correct than scanRow_block16, only, on the aligned/exact-multiple
// common case, faster.
int scanRow_pie_asm(const uint8_t *__restrict rowAlpha3, int panelW, uint32_t *__restrict rowRuns,
                    uint8_t *__restrict cellRow) {
    constexpr int BLOCK = 16;
    if ((reinterpret_cast<uintptr_t>(rowAlpha3) & 0xF) != 0 || (panelW % BLOCK) != 0) {
        return scanRow_block16(rowAlpha3, panelW, rowRuns, cellRow);
    }
    int nRuns = 0;
    int runStart = -1;
    const bool hasCell = cellRow != nullptr;
    for (int x0 = 0; x0 < panelW; x0 += BLOCK) {
        if (!pieBlockNonzero16(rowAlpha3 + static_cast<size_t>(x0) * 3)) {
            if (runStart >= 0) {
                nRuns = emitRun_ref(rowRuns, nRuns, runStart, x0, RUN_GAP_MERGE);
                runStart = -1;
            }
            continue;
        }
        if (hasCell) {
            scanSpanScalar<true>(rowAlpha3 + static_cast<size_t>(x0) * 3, x0, x0 + BLOCK, rowRuns, nRuns, runStart,
                                 cellRow);
        } else {
            scanSpanScalar<false>(rowAlpha3 + static_cast<size_t>(x0) * 3, x0, x0 + BLOCK, rowRuns, nRuns, runStart,
                                  cellRow);
        }
    }
    if (runStart >= 0) {
        nRuns = emitRun_ref(rowRuns, nRuns, runStart, panelW, RUN_GAP_MERGE);
    }
    return nRuns;
}
#endif // defined(__XTENSA__)

// Variant table. Workers append their own entry here (and their function
// lives in this same file) -- the runner and golden-comparison harness pick
// up every row automatically. scanRow_pie_asm's entry is compiled in only
// for an Xtensa target (see the #if above); kScanRowVariantCount adjusts
// automatically since it is sizeof-derived.
const ScanRowVariant kScanRowVariants[] = {
    {"ref_scalar", &scanRow_ref, true, false},
    {"spec", &scanRow_spec, true, false},
    {"block16", &scanRow_block16, true, false},
    {"block32", &scanRow_block32, true, false},
    {"pie_model", &scanRow_pie_model, true, false},
#if defined(__XTENSA__)
    {"pie_asm", &scanRow_pie_asm, false, true},
#endif
};
const int kScanRowVariantCount = sizeof(kScanRowVariants) / sizeof(kScanRowVariants[0]);

} // namespace ovb
