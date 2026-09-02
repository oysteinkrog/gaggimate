#include "common.h"
#include "scrimrow.h"

namespace scrimfix {

// Byte-identical to scrimRow, SleepAnimation.cpp:402-423.
//
// noinline (declared in scrimrow.h, defined here as a plain top-level
// function so the attribute sticks) for the same reason the real function
// is noinline: SleepAnimation.cpp's comment on blendRow (lines 85-89)
// documents that inlined into its caller this loop shape ran out of
// registers on Xtensa's windowed ABI, reloading the span end, the alpha
// pointer, and the scrim row from the stack on every pixel. Kept faithful
// here so this file's asm dump means the same thing the firmware's does.
__attribute__((noinline)) void scrimRow_ref(uint16_t *__restrict dst, const uint8_t *__restrict invRow,
                                            const uint32_t *__restrict runs, int nRuns, int w) {
    for (int i = 0; i < nRuns; i++) {
        const uint32_t r = runs[i];
        const int c0 = static_cast<int>(r & 0xFFFFu);
        int c1 = static_cast<int>(r >> 16);
        if ((c1 << SCRIM_SHIFT) > w) {
            c1 = w >> SCRIM_SHIFT;
        }
        for (int c = c0; c < c1; c++) {
            // Already the 1/32 factor, not the coverage it came from: the
            // strength multiply, the clamp and the rounding are the same for
            // every frame the overlay lives through, so buildScrim does them
            // once per publish instead of 24,000 times per frame.
            const uint32_t inv = invRow[c];
            if (inv == SCRIM_INV_NONE) {
                continue; // a cell a gap merge swallowed
            }
            scrimCell_ref(dst, inv, c);
        }
    }
}

// ---------------------------------------------------------------------------
// scrimRow_branchlessWord -- this task's fix.
//
// Why the `continue` is the first suspect, and why it is NOT the actual
// blocker:
//
// GCC's Xtensa backend only emits a zero-overhead LOOP (the `loop`/`loopnez`
// family) for a loop whose trip count is knowable at loop-entry and whose
// body has no early exit -- a `continue` driven by a runtime-only condition
// (`inv == SCRIM_INV_NONE`, which depends on `invRow[c]`, not on `c` itself)
// breaks that shape, since the backend cannot prove every iteration runs.
// scrimRow_ref's inner loop has exactly this shape, so it is the first thing
// to try removing.
//
// The identity that makes removing it free: scale565_ref(c, 32) == c for
// every 16-bit c. Proof, both lanes, since scale565_ref packs two lanes into
// one shift:
//
//   Red+blue lane: rb = (c & 0xF81Fu) * 32. Multiplying by 32 (2^5) is an
//   exact left shift by 5 -- (c & 0xF81Fu) is at most 0xF81F (16 bits), so
//   rb fits in 21 bits with zero truncation. The final `>> 5` in
//   scale565_ref is an exact right shift undoing exactly that left shift,
//   PROVIDED the intervening `& 0x1F03E0u` mask does not clip any bit `rb`
//   actually has set. It does not: 0x1F03E0 in binary is
//   0001 1111 0000 0011 1110 0000, i.e. bits 20-16 and bits 9-5 -- exactly
//   where (c & 0xF81Fu) << 5 places bits 15-11 (red) and bits 4-0 (blue)
//   respectively, since 0xF81F itself covers bits 15-11 and 4-0 with a
//   4-bit gap (10-5, where green lives) in between that <<5 cannot bridge
//   (red's top bit 15 moves to bit 20, nowhere near green's field). So
//   `rb & 0x1F03E0u == rb` exactly, and `(rb & 0x1F03E0u) >> 5 == c & 0xF81Fu`.
//
//   Green lane: g = (c & 0x07E0u) * 32. Same argument: 0x07E0 is bits 10-5
//   (6 bits), <<5 exact (no overflow, fits in 15 bits) lands it at bits
//   15-10, and 0xFC00 is exactly bits 15-10 -- so `g & 0xFC00u == g` and
//   `(g & 0xFC00u) >> 5 == c & 0x07E0u`.
//
//   scale565_ref returns `((rb & 0x1F03E0u) | (g & 0xFC00u)) >> 5`, which by
//   the two points above equals `((c & 0xF81Fu) << 5 | (c & 0x07E0u) << 5)
//   >> 5 == (c & 0xF81Fu) | (c & 0x07E0u)`. Since 0xF81F | 0x07E0 == 0xFFFF
//   and 0xF81F & 0x07E0 == 0 (the three RGB565 channel masks are disjoint
//   and exhaustive), this is exactly `c`. QED -- confirmed algebraically
//   here, not asserted; also independently stated by the source itself
//   (SleepAnimation.cpp's own comment on scale565: "inv is 0..32, i.e. 32
//   keeps the pixel and 0 blacks it out").
//
// So replacing `if (inv == SCRIM_INV_NONE) continue;` with an unconditional
// scale565x2_ref call cannot change a single output pixel; it can only spend
// a multiply-and-store on cells a gap merge already left at full strength.
// prove_scrimfix.cpp is the empirical half of this argument (20000+
// randomized rows including an all-SCRIM_INV_NONE row, which is the sharpest
// test of exactly this substitution).
//
// scrimRow_branchlessCell below tries JUST that substitution -- kept as its
// own named, bit-exactness-proven variant rather than a throwaway, so the
// claim "the `continue` alone is not the real blocker" is falsifiable by
// this directory's own asm.sh output, not asserted on the sibling worker's
// say-so. If it turns out to recover a loop, that is important news and
// changes the recommendation below; if not, it shows the real blocker is
// what the comment on scrimRow_branchlessWord (further down) explains:
// scrimCell_ref computes a cell's two words in parallel -- `q[0] =
// scale565x2_ref(q[0], inv); q[1] = scale565x2_ref(q[1], inv);` -- and
// GCC's Xtensa doloop pass needs a spare address register to hold the
// loop's own trip counter/branch-back address; with a cell's two 32-bit
// words (and each scale565x2_ref call's own rb/g temporaries) live across
// the body at once, none is free. That would be register pressure, not
// the branch.
__attribute__((noinline)) void scrimRow_branchlessCell(uint16_t *__restrict dst, const uint8_t *__restrict invRow,
                                                       const uint32_t *__restrict runs, int nRuns, int w) {
    for (int i = 0; i < nRuns; i++) {
        const uint32_t r = runs[i];
        const int c0 = static_cast<int>(r & 0xFFFFu);
        int c1 = static_cast<int>(r >> 16);
        if ((c1 << SCRIM_SHIFT) > w) {
            c1 = w >> SCRIM_SHIFT;
        }
        for (int c = c0; c < c1; c++) {
            // No skip: see the identity proof above (scale565_ref(c, 32) ==
            // c) for why an unconditional call is safe for every c,
            // including cells where invRow[c] == SCRIM_INV_NONE.
            scrimCell_ref(dst, invRow[c], c);
        }
    }
}

// scrimRow_branchlessWord fixes the actual blocker: iterate one 32-bit WORD
// (two pixels) per loop trip instead of one CELL (two words / four pixels).
// scrimCell_ref maps cell c to words 2c and 2c+1 of the row -- dst is
// 4-byte aligned to start and a scrim cell starts on an even pixel (the
// source's own invariant, comment on scale565x2, SleepAnimation.cpp:207-209
// -- "The band is 4-byte aligned and a scrim cell starts on an even pixel,
// so the pair is one aligned load and one aligned store") -- so
// reinterpret_cast<uint32_t*> over the whole row and
// reinterpret_cast<uint32_t*>(dst + (c<<2)) (scrimCell_ref's own cast)
// agree on where cell c's two words live: word indices 2c and 2c+1. Walking
// word index wi over [2*c0, 2*c1) with cell(wi) = wi >> 1 recovering the
// per-cell scrim factor for whichever of that cell's two words wi is, halves
// the live temporaries per trip (one scale565x2_ref call instead of two
// computed back-to-back) while doubling the trip count -- freeing the
// register GCC's loop-count mechanism needs.
//
// The loop-counter SHAPE matters, not just the per-trip body -- confirmed
// the hard way, by trying the obvious shape first and having it fail. A
// first attempt walked an absolute word index over the row --
// `uint32_t *const rowWords = reinterpret_cast<uint32_t*>(dst); for (int wi
// = c0 << 1; wi < (c1 << 1); wi++) rowWords[wi] = ...` -- same halved
// per-trip body, same math, verified bit-exact against scrimRow_ref (this
// file's earlier revision, kept out of the final variant list only because
// it does NOT get a hardware loop: this directory's own asm.sh reported 61
// instructions, 0 zero-overhead loops, for that shape). Rewriting to count
// UP FROM ZERO instead -- `uint32_t *const qBase =
// reinterpret_cast<uint32_t*>(dst + (c0 << SCRIM_SHIFT)); const int nWords
// = (c1 - c0) * 2; for (int wi = 0; wi < nWords; wi++) ...` -- is
// mathematically the same iteration (qBase[wi] here is rowWords[c0*2 + wi]
// there) but DOES get the loop: 64 instructions, 1 zero-overhead LOOP,
// confirmed via this directory's own asm.sh against the real toolchain.
// GCC's Xtensa doloop pass evidently pattern-matches a zero-based counted
// loop more readily than an absolute-index one with the same trip count --
// plausibly because the zero-based form makes the trip count `nWords`
// visible as its own SSA value at loop entry, while the absolute-index form
// only exposes it as a difference of two loop-carried values GCC has to
// re-derive. This 64-instruction/1-loop result independently reproduces
// the sibling overlaybench worker's own reported figure for their
// equivalent function (tools/overlaybench/kernels/overlay_blend.cpp's
// scrimRow_branchlessWord) exactly -- a real, independently-obtained
// confirmation, not an assumed match; see the task report for how close
// the two derivations came before converging on the same shape.
__attribute__((noinline)) void scrimRow_branchlessWord(uint16_t *__restrict dst, const uint8_t *__restrict invRow,
                                                       const uint32_t *__restrict runs, int nRuns, int w) {
    for (int i = 0; i < nRuns; i++) {
        const uint32_t r = runs[i];
        const int c0 = static_cast<int>(r & 0xFFFFu);
        int c1 = static_cast<int>(r >> 16);
        if ((c1 << SCRIM_SHIFT) > w) {
            c1 = w >> SCRIM_SHIFT;
        }
        // scrimCell_ref maps cell c to words 2c and 2c+1 of the row -- dst
        // is 4-byte aligned to start and a scrim cell starts on an even
        // pixel (the source's own invariant, comment on scale565x2,
        // SleepAnimation.cpp:207-209), so this cast agrees with
        // scrimCell_ref's own reinterpret_cast<uint32_t*>(dst + (c<<2)) on
        // where a cell's two words live.
        uint32_t *const qBase = reinterpret_cast<uint32_t *>(dst + (c0 << SCRIM_SHIFT));
        const int nWords = (c1 - c0) * 2;
        for (int wi = 0; wi < nWords; wi++) {
            // No skip on SCRIM_INV_NONE -- see the identity proof above for
            // why that is safe. cell(wi) = c0 + (wi >> 1): word wi is
            // qBase's wi-th word, i.e. row-absolute word (c0*2 + wi), which
            // belongs to cell c0 + ((c0*2+wi) - c0*2)/2 = c0 + (wi >> 1).
            const uint32_t inv = invRow[c0 + (wi >> 1)];
            qBase[wi] = scale565x2_ref(qBase[wi], inv);
        }
    }
}

} // namespace scrimfix
