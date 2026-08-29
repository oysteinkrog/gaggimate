#include "overlay_blend.h"

namespace ovb {

// Byte-identical to blendRow in SleepAnimation.cpp. noinline is load-bearing,
// not a style choice: the source comment on blendRow explains that inlined
// into its caller, the loop ran out of registers on Xtensa's windowed ABI and
// a five-instruction body cost sixty cycles. Dropping this attribute would
// make the asm dump (and any register-pressure conclusions drawn from it)
// unfaithful to what the firmware actually ships.
__attribute__((noinline)) void blendRow_ref(uint16_t *__restrict dst, const uint8_t *__restrict colour,
                                            const uint32_t *__restrict runs, int nRuns) {
    for (int i = 0; i < nRuns; i++) {
        const uint32_t r = runs[i];
        int x = static_cast<int>(r & 0xFFFFu);
        const int xEnd = static_cast<int>(r >> 16);
        const uint8_t *px = colour + static_cast<size_t>(x) * 3;
        for (; x < xEnd; x++, px += 3) {
            const uint32_t a = px[2];
            if (a == 0) {
                continue;
            }
            const uint16_t c = static_cast<uint16_t>(px[0] | (px[1] << 8));
            dst[x] = a == 255 ? c : blend565_ref(c, dst[x], static_cast<uint8_t>(a));
        }
    }
}

// scrimCell, inlined (SCRIM_SHIFT==2, four pixels, spelled out like the
// firmware to keep the compiler from re-introducing the counted loop the
// real code deliberately avoids).
__attribute__((always_inline)) static inline void scrimCell_ref(uint16_t *__restrict dst, uint32_t inv, int c) {
    uint32_t *const q = reinterpret_cast<uint32_t *>(dst + (c << SCRIM_SHIFT));
    q[0] = scale565x2_ref(q[0], inv);
    q[1] = scale565x2_ref(q[1], inv);
}

// Byte-identical to scrimRow in SleepAnimation.cpp. noinline for the same
// register-pressure reason as blendRow_ref above.
__attribute__((noinline)) void scrimRow_ref(uint16_t *__restrict dst, const uint8_t *__restrict invRow,
                                            const uint32_t *__restrict haloRuns, int nHalo, int w) {
    for (int i = 0; i < nHalo; i++) {
        const uint32_t r = haloRuns[i];
        const int c0 = static_cast<int>(r & 0xFFFFu);
        int c1 = static_cast<int>(r >> 16);
        if ((c1 << SCRIM_SHIFT) > w) {
            c1 = w >> SCRIM_SHIFT;
        }
        for (int c = c0; c < c1; c++) {
            const uint32_t inv = invRow[c];
            if (inv == SCRIM_INV_NONE) {
                continue;
            }
            scrimCell_ref(dst, inv, c);
        }
    }
}

// Direction 1 (see the header comment): scrimRow_ref with the per-cell
// `continue` on SCRIM_INV_NONE replaced by an unconditional scrimCell_ref
// call. 32 is scale565_ref's identity factor -- (c & mask) * 32 >> 5 == c
// exactly, for every c, since >>5 exactly undoes the *32 -- so this cannot
// change the pixels a gap-merge left at full strength; it can only spend an
// extra multiply-and-store on them. That is precisely the branchless
// substitution the shipped scrimRowPie already makes for the vector lane
// that cannot skip ("Cells the halo's gap merge swallowed are dimmed by
// SCRIM_INV_NONE here rather than skipped, because a lane cannot branch.
// That factor is an exact identity -- 32/32 -- so the group writes those
// pixels back unchanged" -- SleepAnimation.cpp, comment on scrimRowPie).
//
// This alone does NOT recover a hardware LOOP (checked via ./asm.sh
// overlay_blend: still 0 zero-overhead loops, 97 instructions -- MORE than
// scrimRow_ref's 94, since removing the skip means the ~13-instruction cell
// body now always runs). That disproves BASELINE-OVERLAY.md's stated guess
// that the `continue` alone was blocking the LOOP: blendRow_ref keeps two
// per-pixel branches (a==0, a==255) and still gets a LOOP, so a branch
// inside a fixed-trip-count body is not automatically disqualifying.
// Comparing the two loop bodies directly points at the real blocker
// instead: scrimCell_ref computes BOTH of a cell's two words in parallel (8
// mul16u instructions, most of a13-a15 and more live across the body) --
// register pressure GCC's doloop pass has no spare address register left
// to service, not the branch. See scrimRow_branchlessWord below, which
// tests that directly and confirms it. Kept registered anyway: it is still
// bit-exact and a legitimate (if, per the host numbers, not obviously
// winning) alternative for the coordinator to compare.
__attribute__((noinline)) void scrimRow_branchless(uint16_t *__restrict dst, const uint8_t *__restrict invRow,
                                                   const uint32_t *__restrict haloRuns, int nHalo, int w) {
    for (int i = 0; i < nHalo; i++) {
        const uint32_t r = haloRuns[i];
        const int c0 = static_cast<int>(r & 0xFFFFu);
        int c1 = static_cast<int>(r >> 16);
        if ((c1 << SCRIM_SHIFT) > w) {
            c1 = w >> SCRIM_SHIFT;
        }
        for (int c = c0; c < c1; c++) {
            scrimCell_ref(dst, invRow[c], c);
        }
    }
}

// scrimRow_branchless restructured to iterate one WORD (two pixels) per
// trip instead of one CELL (two words / four pixels, scrimCell_ref's unit),
// keeping the same branchless SCRIM_INV_NONE substitution. This halves the
// live temporaries in the loop body (one scale565x2_ref call per iteration
// instead of two computed in parallel) and is the direct test of the
// register-pressure theory above: ./asm.sh overlay_blend confirms it --
// 1 zero-overhead LOOP recovered, 64 instructions total (vs 94 for
// scrimRow_ref and 97 for scrimRow_branchless). Same identity argument as
// scrimRow_branchless justifies the branchless substitution; doubling the
// trip count while halving the per-trip body is what actually frees a
// register for GCC's loop-count mechanism -- see the report for host
// timing and why this, not scrimRow_branchless, is the variant worth
// carrying to a device measurement.
__attribute__((noinline)) void scrimRow_branchlessWord(uint16_t *__restrict dst, const uint8_t *__restrict invRow,
                                                       const uint32_t *__restrict haloRuns, int nHalo, int w) {
    for (int i = 0; i < nHalo; i++) {
        const uint32_t r = haloRuns[i];
        const int c0 = static_cast<int>(r & 0xFFFFu);
        int c1 = static_cast<int>(r >> 16);
        if ((c1 << SCRIM_SHIFT) > w) {
            c1 = w >> SCRIM_SHIFT;
        }
        uint32_t *const qBase = reinterpret_cast<uint32_t *>(dst + (c0 << SCRIM_SHIFT));
        const int nWords = (c1 - c0) * 2;
        for (int wi = 0; wi < nWords; wi++) {
            const uint32_t inv = invRow[c0 + (wi >> 1)];
            qBase[wi] = scale565x2_ref(qBase[wi], inv);
        }
    }
}

// ---------------------------------------------------------------------------
// Direction 2 (see the header comment): a vectorisable design for pass 2,
// the composite. Unlike the dim pass, blendRow has no shipped PIE precedent
// to reproduce -- this is new ground, for two reasons that make it harder
// than scale565Oct's case:
//
//   1. Alpha is per-pixel, not one shared factor for a whole cell -- so the
//      vector multiply needs "eight factors, one per pixel" the way
//      scale565Oct already loads q5 (see its comment), but there is no
//      per-cell grid to source it from; it has to be gathered straight out
//      of the 3-byte-stride overlay snapshot, same as the colour itself.
//   2. blend565's math is `(fg*a + bg*inv) >> 8` -- a SUM of two products
//      before the shift, not scale565's single product. That matters
//      because it rules out doing this with two independent
//      multiply-then-shift steps: (A>>8)+(B>>8) is not (A+B)>>8 in general
//      (take A=B=200: (200+200)>>8=1, but (200>>8)+(200>>8)=0). The shift
//      has to happen once, after the add.
//
// Correctness argument for the design below (see blendRow_pie_model and
// blendPixelGeneralModel):
//
//   a) blend565_ref's three channel computations are already independent
//      of each other (R, G, B never share a multiply the way scale565
//      packs R+B into one product) -- they just operate on the channel's
//      *bit-positioned* value (e.g. R as 0..0xF800) instead of its raw
//      0..31 magnitude. Re-deriving on the raw magnitude instead changes
//      nothing: for channel raw values X (fg) and B (bg) at bit position P
//      (P=11 for R, 5 for G, 0 for B), blend565_ref computes
//      Y = X*a + B*inv (with X,B already shifted left by P), takes Y>>8,
//      and masks to the channel's bits. Since X = Xraw<<P, Y = (Xraw*a +
//      Braw*inv) << P = Yraw << P, so Y>>8 = Yraw << (P-8) when P>=8, or
//      Yraw's low bits move down when P<8 -- either way, masking the
//      channel's own bits back out of Y>>8 recovers exactly
//      floor(Yraw/256) << P (worked through the R example in the block
//      comment above scrimRowPie's header note; the same shift-and-mask
//      algebra applies to every channel here). So computing on raw
//      magnitudes 0..31/0..63 with a plain `>>8` and re-positioning
//      afterward (blendPixelGeneralModel below) is bit-for-bit the same
//      arithmetic blend565_ref does, just with smaller intermediate values
//      -- Xraw*a maxes at 31*255=7905 and Braw*inv at 31*256=7936, both
//      comfortably inside 16 bits, which is what makes this representable
//      with the same "multiply eight 16-bit lanes, truncate to 16 bits"
//      primitive scale565Oct's EE.VMUL.U16 already demonstrates (SAR=0 this
//      time, since nothing should be shifted until after the two products
//      are summed) instead of needing a wider intermediate.
//
//   b) a==255 is NOT covered by (a). Working example: fg raw R = 31 (max),
//      bg raw R = 1, a = 255 (so inv = 1): blend565_ref computes
//      r = ((0xF800*255) + (0x0800*1)) >> 8 = 63248, and 63248 & 0xF800 =
//      0xF000 -- raw output R = 30, not fg's 31. The formula is exact
//      everywhere else, but at a=255 it is one 256th short of full
//      replacement, because 255 (the largest representable a) is not 256
//      (the value that would make the weighted average exactly reproduce
//      fg). That is exactly why the scalar blendRow_ref special-cases
//      a==255 as a direct copy instead of calling blend565_ref -- and it
//      means a "branchless full computation" is not just an optimization
//      question for this one case, it is a correctness requirement: no
//      per-channel formula run uniformly over a group can reproduce both
//      outcomes at once for a mixed group.
//
// The design consequence: group 8 pixels, gather them (unavoidably scalar,
// stride-3 source, same reason scrimRowPie needs expandScrimInv's scalar
// pre-pass), then take one of three group-level paths, decided once per
// group of 8 rather than once per pixel:
//   - every lane a==255            -> straight copy (no arithmetic at all)
//   - no lane a==255                -> blendPixelGeneralModel on all eight
//                                      (safe per (a); a==0 lanes fall out
//                                      as an exact identity the same way,
//                                      see the note on blendPixelGeneralModel)
//   - a mix of the two               -> scalar fallback for just this group,
//                                      byte-identical to blendRow_ref's own
//                                      per-pixel body (blendPixelScalar)
// A run's non-multiple-of-8 ends (prologue before the first aligned
// boundary, epilogue after the last) use the same scalar fallback. Grouping
// on 8-pixel (16-byte) boundaries of the ABSOLUTE row position -- not
// relative to the run's own start -- is deliberate: it is what a real
// 128-bit vector load/store needs (the row buffer starts 16-byte aligned,
// same as scrimRowPie relies on), and it is what blendRow_pie_asm has to
// reproduce group-for-group, since this function is the bit-exactness gate
// for that asm's arithmetic, the same relationship scanRow_pie_model has to
// scanRow_pie_asm.

// Shared per-pixel body for every scalar fallback path below (prologue,
// epilogue, and mixed groups) -- byte-identical to the inner-loop body in
// blendRow_ref, factored out so those fallbacks use exactly this code
// rather than a parallel reimplementation of it (see scanSpanScalar in
// span_scan.cpp for the same reasoning).
__attribute__((always_inline)) static inline void blendPixelScalar(uint16_t *__restrict dst,
                                                                    const uint8_t *__restrict px, int x) {
    const uint32_t a = px[2];
    if (a == 0) {
        return; // only the few pixels a gap merge swallowed
    }
    const uint16_t c = static_cast<uint16_t>(px[0] | (px[1] << 8));
    dst[x] = a == 255 ? c : blend565_ref(c, dst[x], static_cast<uint8_t>(a));
}

// blend565_ref, re-derived on each channel's raw 0..31/0..63 magnitude
// instead of its bit-positioned value -- see argument (a) above. Exact
// substitute for blend565_ref for any a in [0,254]; must NEVER be called
// with a==255 (see argument (b) above) -- blendRow_pie_model only calls
// this from the "no lane opaque" group path, which already excludes it.
__attribute__((always_inline)) static inline uint16_t blendPixelGeneralModel(uint16_t fg, uint16_t bg, uint32_t a) {
    const uint32_t inv = 256u - a;
    const uint32_t fgR = (fg >> 11) & 0x1Fu, bgR = (bg >> 11) & 0x1Fu;
    const uint32_t fgG = (fg >> 5) & 0x3Fu, bgG = (bg >> 5) & 0x3Fu;
    const uint32_t fgB = fg & 0x1Fu, bgB = bg & 0x1Fu;
    const uint32_t outR = (fgR * a + bgR * inv) >> 8;
    const uint32_t outG = (fgG * a + bgG * inv) >> 8;
    const uint32_t outB = (fgB * a + bgB * inv) >> 8;
    return static_cast<uint16_t>((outR << 11) | (outG << 5) | outB);
}

// Pure-C model of the vector composite design derived above. Registered as
// a variant (via blendStage_pieModel / blendStage_pieModelCombined below)
// with bitExactRequired=true, unlike blendRow_pie_asm: this is what the
// golden compare actually proves correct.
__attribute__((noinline)) void blendRow_pie_model(uint16_t *__restrict dst, const uint8_t *__restrict colour,
                                                  const uint32_t *__restrict runs, int nRuns) {
    for (int i = 0; i < nRuns; i++) {
        const uint32_t r = runs[i];
        int x = static_cast<int>(r & 0xFFFFu);
        const int xEnd = static_cast<int>(r >> 16);

        int xAlignedStart = (x + 7) & ~7;
        if (xAlignedStart > xEnd) {
            xAlignedStart = xEnd;
        }
        const int xAlignedEnd = xAlignedStart + ((xEnd - xAlignedStart) & ~7);

        // Prologue: pixels before the first 8-pixel-aligned boundary.
        {
            const uint8_t *px = colour + static_cast<size_t>(x) * 3;
            for (; x < xAlignedStart; x++, px += 3) {
                blendPixelScalar(dst, px, x);
            }
        }
        // Body: full 8-pixel groups, one group-level decision each.
        for (; x < xAlignedEnd; x += 8) {
            uint16_t colStage[8];
            uint8_t alphaStage[8];
            bool allOpaque = true;
            bool anyOpaque = false;
            {
                const uint8_t *gp = colour + static_cast<size_t>(x) * 3;
                for (int k = 0; k < 8; k++, gp += 3) {
                    colStage[k] = static_cast<uint16_t>(gp[0] | (gp[1] << 8));
                    const uint8_t a = gp[2];
                    alphaStage[k] = a;
                    if (a == 255) {
                        anyOpaque = true;
                    } else {
                        allOpaque = false;
                    }
                }
            }
            if (allOpaque) {
                for (int k = 0; k < 8; k++) {
                    dst[x + k] = colStage[k];
                }
            } else if (!anyOpaque) {
                for (int k = 0; k < 8; k++) {
                    dst[x + k] = blendPixelGeneralModel(colStage[k], dst[x + k], alphaStage[k]);
                }
            } else {
                // Mixed group (some lanes exactly opaque, some not): no
                // group-level shortcut is safe, per argument (b) above --
                // fall back to exactly blendRow_ref's per-pixel body.
                const uint8_t *px = colour + static_cast<size_t>(x) * 3;
                for (int k = 0; k < 8; k++, px += 3) {
                    blendPixelScalar(dst, px, x + k);
                }
            }
        }
        // Epilogue: pixels after the last 8-pixel-aligned boundary.
        {
            const uint8_t *px = colour + static_cast<size_t>(x) * 3;
            for (; x < xEnd; x++, px += 3) {
                blendPixelScalar(dst, px, x);
            }
        }
    }
}

// The two-pass row body from renderFrame, scrim gated the same way (nHalo
// checked before touching invRow/haloRuns at all -- matching the firmware,
// where a whole-row-transparent cell row costs nothing beyond the branch).
// noinline: renderFrame is a large function that would not inline these
// calls either; keeping the same shape here avoids the single-TU asm dump
// (kernels compiled alone, unlike the real multi-thousand-line
// SleepAnimation.cpp) reaching a different inlining decision than the
// firmware does.
__attribute__((noinline)) void blendStage_ref(uint16_t *__restrict dst, const uint8_t *__restrict invRow,
                                              const uint32_t *__restrict haloRuns, int nHalo,
                                              const uint8_t *__restrict colour, const uint32_t *__restrict runs,
                                              int nRuns, int w) {
    if (nHalo != 0) {
        scrimRow_ref(dst, invRow, haloRuns, nHalo, w);
    }
    if (nRuns != 0) {
        blendRow_ref(dst, colour, runs, nRuns);
    }
}

// Direction 1 only: scrimRow_branchless + blendRow_ref. Isolates the
// recovered hardware loop from the composite change below.
__attribute__((noinline)) void blendStage_scrimBranchless(uint16_t *__restrict dst, const uint8_t *__restrict invRow,
                                                          const uint32_t *__restrict haloRuns, int nHalo,
                                                          const uint8_t *__restrict colour,
                                                          const uint32_t *__restrict runs, int nRuns, int w) {
    if (nHalo != 0) {
        scrimRow_branchless(dst, invRow, haloRuns, nHalo, w);
    }
    if (nRuns != 0) {
        blendRow_ref(dst, colour, runs, nRuns);
    }
}

// Direction 1, the variant that actually recovers a hardware loop:
// scrimRow_branchlessWord + blendRow_ref.
__attribute__((noinline)) void blendStage_scrimBranchlessWord(uint16_t *__restrict dst,
                                                               const uint8_t *__restrict invRow,
                                                               const uint32_t *__restrict haloRuns, int nHalo,
                                                               const uint8_t *__restrict colour,
                                                               const uint32_t *__restrict runs, int nRuns, int w) {
    if (nHalo != 0) {
        scrimRow_branchlessWord(dst, invRow, haloRuns, nHalo, w);
    }
    if (nRuns != 0) {
        blendRow_ref(dst, colour, runs, nRuns);
    }
}

// Direction 2 only: scrimRow_ref + blendRow_pie_model. Isolates the vector
// composite model from the scrim change above.
__attribute__((noinline)) void blendStage_pieModel(uint16_t *__restrict dst, const uint8_t *__restrict invRow,
                                                   const uint32_t *__restrict haloRuns, int nHalo,
                                                   const uint8_t *__restrict colour, const uint32_t *__restrict runs,
                                                   int nRuns, int w) {
    if (nHalo != 0) {
        scrimRow_ref(dst, invRow, haloRuns, nHalo, w);
    }
    if (nRuns != 0) {
        blendRow_pie_model(dst, colour, runs, nRuns);
    }
}

// Both directions together, using scrimRow_branchlessWord (the direction-1
// variant that actually recovers a hardware loop, not scrimRow_branchless).
__attribute__((noinline)) void blendStage_pieModelCombined(uint16_t *__restrict dst, const uint8_t *__restrict invRow,
                                                           const uint32_t *__restrict haloRuns, int nHalo,
                                                           const uint8_t *__restrict colour,
                                                           const uint32_t *__restrict runs, int nRuns, int w) {
    if (nHalo != 0) {
        scrimRow_branchlessWord(dst, invRow, haloRuns, nHalo, w);
    }
    if (nRuns != 0) {
        blendRow_pie_model(dst, colour, runs, nRuns);
    }
}

#if defined(__XTENSA__)
// ---------------------------------------------------------------------------
// Real ESP32-S3 PIE attempt at blendRow_pie_model's design. Xtensa-only
// inline asm: cannot build, and has never been run, on this x86 host -- see
// piePending on this variant's kBlendStageVariants entry.
//
// Mnemonic groundwork (this is what "checked by feeding them to the real
// assembler" actually means -- ground truth is xtensa-esp32s3-elf-as, run
// via a standalone probe file, not this repo's own dumps, since none of
// this existed here before): every mnemonic scale565Oct already uses
// (ee.vld.128.ip, ee.vst.128.ip, ee.andq, ee.orq, ee.vmul.u16) reconfirmed
// fine. blendRow_pie_model's general-path arithmetic additionally needs to
// sum fg*a and bg*inv before the one shift-by-8 -- scale565Oct never needed
// an add at all, so there was no mnemonic to crib. Checked against the real
// assembler and CONFIRMED to exist: ee.vadds.s16 (signed 16-bit vector add,
// saturating -- safe here regardless, since every sum this kernel would
// ever feed it is bounded by 31*255 + 31*256 < 32768, so the sum never
// reaches the range where saturating and wrapping addition would differ).
// Also confirmed to exist, unused below: ee.vcmp.eq.s16, ee.vunzip.16,
// ee.vzip.16, ee.vmulas.s16.qacc, ee.zero.qacc. Confirmed NOT to exist
// under any spelling tried: ee.addq/ee.vaddq.s16/ee.add.s16 (no plain
// non-saturating add), ee.vsubq/ee.vsub.s16/ee.subq (no subtract),
// ee.vseleqz.s16 (no select-on-zero under that name), and two guesses at a
// QACC-extraction opcode (ee.ld.qacc.h.l.128.ip, ee.srs.qacc).
//
// Given ee.vadds.s16 exists, the general path COULD be built: extract each
// channel's raw 0..31/0..63 magnitude via ee.andq + ee.vmul.u16-as-a-
// right-shift (multiply by a broadcast 1 with `ssai` set to the channel's
// bit position, exactly blendPixelGeneralModel's derivation, vectorised),
// multiply by a/inv at SAR=0 (both terms fit 16 bits per the bound above),
// ee.vadds.s16 the two terms, ee.vmul.u16 again at SAR=8 for the final
// divide, then ee.vmul.u16 by a channel-specific power-of-two constant at
// SAR=0 to reposition the raw result back into its bit field (multiplying
// by 2^11/2^5/1 this way is a left-shift, since ee.vmul.u16 only ever
// shifts right -- the same "multiply as a shift" trick used for the
// extraction step, run in the other direction) -- three channels' worth,
// each needing several `ssai` changes to move SAR between 11/5/0/8, hand-
// scheduled through what is probably an eight-q-register file. That is a
// long, easy-to-get-subtly-wrong instruction sequence with no way to test
// it beyond "does it assemble" (no device or QEMU run available here), so
// it was judged too likely to ship a silently-wrong result for this pass.
// Left unattempted, not attempted-and-hoped: the confirmed mnemonics above
// are the concrete starting point for whoever picks this up next, with a
// real device or QEMU run as a hard prerequisite before it ships either way.
//
// What IS implemented: the "every lane in the group is opaque" path, which
// needs none of the above -- it is a plain aligned copy (no arithmetic to
// get wrong), gathered into a 16-byte-aligned staging buffer as usual (no
// gather instruction exists in this ISA, matching scanRow_pie_asm and
// scrimRowPie's own precedent) and written with one EE.VLD.128/EE.VST.128
// pair. Any group that is not all-opaque falls back to blendPixelScalar,
// proven correct by the golden compare -- so this function is always at
// least as correct as blendRow_pie_model, never less, the same relationship
// scanRow_pie_asm has to scanRow_block16.
void blendRow_pie_asm(uint16_t *__restrict dst, const uint8_t *__restrict colour, const uint32_t *__restrict runs,
                      int nRuns) {
    for (int i = 0; i < nRuns; i++) {
        const uint32_t r = runs[i];
        int x = static_cast<int>(r & 0xFFFFu);
        const int xEnd = static_cast<int>(r >> 16);

        int xAlignedStart = (x + 7) & ~7;
        if (xAlignedStart > xEnd) {
            xAlignedStart = xEnd;
        }
        const int xAlignedEnd = xAlignedStart + ((xEnd - xAlignedStart) & ~7);

        {
            const uint8_t *px = colour + static_cast<size_t>(x) * 3;
            for (; x < xAlignedStart; x++, px += 3) {
                blendPixelScalar(dst, px, x);
            }
        }
        for (; x < xAlignedEnd; x += 8) {
            alignas(16) uint16_t colStage[8];
            bool allOpaque = true;
            {
                const uint8_t *gp = colour + static_cast<size_t>(x) * 3;
                for (int k = 0; k < 8; k++, gp += 3) {
                    colStage[k] = static_cast<uint16_t>(gp[0] | (gp[1] << 8));
                    if (gp[2] != 255) {
                        allOpaque = false;
                    }
                }
            }
            if (allOpaque) {
                const uint16_t *src = colStage;
                uint16_t *wr = dst + x;
                asm volatile("ee.vld.128.ip q0, %[src], 16\n"
                             "ee.vst.128.ip q0, %[wr], 16\n"
                             : [src] "+r"(src), [wr] "+r"(wr)
                             :
                             : "memory");
            } else {
                const uint8_t *px = colour + static_cast<size_t>(x) * 3;
                for (int k = 0; k < 8; k++, px += 3) {
                    blendPixelScalar(dst, px, x + k);
                }
            }
        }
        {
            const uint8_t *px = colour + static_cast<size_t>(x) * 3;
            for (; x < xEnd; x++, px += 3) {
                blendPixelScalar(dst, px, x);
            }
        }
    }
}

void blendStage_pieAsm(uint16_t *__restrict dst, const uint8_t *__restrict invRow, const uint32_t *__restrict haloRuns,
                       int nHalo, const uint8_t *__restrict colour, const uint32_t *__restrict runs, int nRuns,
                       int w) {
    if (nHalo != 0) {
        scrimRow_branchlessWord(dst, invRow, haloRuns, nHalo, w);
    }
    if (nRuns != 0) {
        blendRow_pie_asm(dst, colour, runs, nRuns);
    }
}
#endif // defined(__XTENSA__)

const BlendStageVariant kBlendStageVariants[] = {
    {"ref_scalar", &blendStage_ref, true, false},
    {"scrim_branchless", &blendStage_scrimBranchless, true, false},
    {"scrim_branchless_word", &blendStage_scrimBranchlessWord, true, false},
    {"blend_pie_model", &blendStage_pieModel, true, false},
    {"combined_pie_model", &blendStage_pieModelCombined, true, false},
#if defined(__XTENSA__)
    {"pie_asm", &blendStage_pieAsm, false, true},
#endif
};
const int kBlendStageVariantCount = sizeof(kBlendStageVariants) / sizeof(kBlendStageVariants[0]);

} // namespace ovb
