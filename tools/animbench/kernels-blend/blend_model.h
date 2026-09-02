// Layer 1 (blendPixelGeneral_model) and Layer 2 (blendRow_group8_model) of
// the blendRow vectorization design. Both are pure C++, no Xtensa
// dependency -- Layer 3's real asm (blend_pie_kernel.cpp) has to reproduce
// Layer 2's group-dispatch structure and Layer 1's per-pixel arithmetic
// exactly; these two layers are what the interpreter (blend_interp.h) and
// the asm are proven against.
//
// -----------------------------------------------------------------------
// Layer 1 derivation (independent re-derivation -- see the report for how
// this compares to the one sketched in tools/overlaybench/kernels/
// overlay_blend.cpp's blendPixelGeneralModel; the algebra has exactly one
// correct answer here, so convergence on the same formula is expected, not
// copied).
//
// blend565_ref (blend_ref.h, verbatim from SleepAnimation.cpp:154-184)
// computes, per channel, on the channel's BIT-POSITIONED value -- e.g. red
// as one of {0, 0x0800, 0x1000, ..., 0xF800}, not as 0..31:
//
//   Y_bitpos = ((fg_bitpos * a) + (bg_bitpos * inv)) >> 8,  inv = 256 - a
//   channel_out = Y_bitpos & channel_mask
//
// Claim: for a channel at bit position P (P=11 red, P=5 green, P=0 blue),
// letting Xraw = fg_bitpos >> P and Braw = bg_bitpos >> P (i.e. fg_bitpos =
// Xraw << P exactly, since the mask already isolates only that channel's
// bits), the following holds for every a in [0, 254]:
//
//   channel_out == ((Xraw * a + Braw * inv) >> 8) << P     ... (*)
//
// Proof: fg_bitpos * a = (Xraw << P) * a = (Xraw * a) << P, and likewise
// for bg_bitpos * inv. So:
//
//   Y_bitpos = ((Xraw*a) << P) + ((Braw*inv) << P) = (Xraw*a + Braw*inv) << P
//
// Right-shifting a value that is some quantity Q left-shifted by P, by 8,
// is not simply "shift Q by (8-P)" in general (integer shifts don't
// commute with addition-of-shifted-terms unless both terms share the same
// shift, which they do here -- Q is already the *sum* Xraw*a + Braw*inv,
// computed before either shift). Concretely: Y_bitpos = Q << P where
// Q = Xraw*a + Braw*inv, so Y_bitpos >> 8 = (Q << P) >> 8. For P >= 8 this
// is exactly Q << (P-8); for P < 8 (green P=5, blue P=0) it is
// Q >> (8-P) with the low (8-P) bits of Q shifted out -- but those low
// bits are exactly what channel_mask discards next (channel_mask's low P
// bits are always zero, and Q's own low (8-P) bits, once repositioned to
// bit P by the eventual << P, would land below bit P and get masked away
// regardless of whether the >>8 or the final AND was the one to drop
// them). Either way, after `& channel_mask`, the surviving bits are
// exactly floor(Q / 256) with its top bits re-aligned to position P, i.e.
// exactly ((Q >> 8) << P) & channel_mask = (((Xraw*a + Braw*inv) >> 8) &
// channel_range) << P, which is statement (*) (channel_range being 0x1F or
// 0x3F, the mask already restricted to that channel's own bit window by
// construction of Xraw/Braw, so the final `<< P` alone reproduces
// channel_out bit-for-bit once ANDed against channel_mask -- masking
// commutes with this whole derivation because every step here only ever
// touches bits within the one channel's own window).
//
// So computing on raw magnitudes (Xraw, Braw in 0..31 or 0..63) with a
// plain `>>8` and repositioning the result to bit P afterward reproduces
// blend565_ref's per-channel output bit-for-bit, FOR a IN [0,254].
//
// a==254 is the tightest boundary case worth checking by hand: with
// Xraw=31 (max), a=254, Braw=0, inv=2: Q = 31*254 + 0 = 7874,
// Q>>8 = 30 (7874/256 = 30.76...). blend565_ref computes on bit-positioned
// values directly: fg_bitpos=0xF800, a=254: (0xF800*254 + 0) >> 8 =
// (0xF800*254)>>8. 0xF800*254 = 63488*254 = 16,125,952 = 0xF61C00.
// >>8 = 0xF61C, & 0xF800 = 0xF000 -- raw output = 0xF000>>11 = 30. Matches.
//
// a==255 is excluded (see blend_ref.h and the header comment in
// blend_pie_kernel.cpp for the worked a==255 counterexample: at full alpha
// the weighted-average formula is provably one 256th short of exactly
// reproducing fg, which is exactly why blendRow_ref special-cases it as a
// direct copy rather than calling blend565_ref at all). blendPixelGeneral_
// model below must NEVER be called with a==255; the group-dispatch layer
// (blendRow_group8_model) enforces that by construction, same as
// blendRow_pie_model does in the sibling's file.
//
// a==0 is a special case worth confirming rather than assuming: inv=256,
// so Braw*inv=Braw*256, and (0 + Braw*256)>>8 == Braw exactly (256 divides
// evenly, no truncation) -- i.e. the formula is an exact identity at a==0,
// reproducing bg unchanged. This matters for Layer 2: unlike blendRow_ref
// (which `continue`s on a==0, touching dst not at all), the vectorized
// general path below runs every non-opaque lane -- including a==0 lanes --
// through this same arithmetic uniformly. That's only safe because of this
// exact-identity property, confirmed here rather than assumed.
#pragma once
#include "blend_ref.h"
#include <cstdint>

namespace blendopt {

// ---------------------------------------------------------------------
// Layer 1: per-pixel general-blend formula on raw channel magnitudes.
// Exact substitute for blend565_ref for any a in [0,254]; MUST NEVER be
// called with a==255 (see the derivation above and the counterexample in
// blend_pie_kernel.cpp).
__attribute__((always_inline)) inline uint16_t blendPixelGeneral_model(uint16_t fg, uint16_t bg, uint32_t a) {
    const uint32_t inv = 256u - a;
    const uint32_t fgR = (fg >> 11) & 0x1Fu, bgR = (bg >> 11) & 0x1Fu;
    const uint32_t fgG = (fg >> 5) & 0x3Fu, bgG = (bg >> 5) & 0x3Fu;
    const uint32_t fgB = fg & 0x1Fu, bgB = bg & 0x1Fu;
    const uint32_t outR = (fgR * a + bgR * inv) >> 8;
    const uint32_t outG = (fgG * a + bgG * inv) >> 8;
    const uint32_t outB = (fgB * a + bgB * inv) >> 8;
    return static_cast<uint16_t>((outR << 11) | (outG << 5) | outB);
}

// ---------------------------------------------------------------------
// Layer 2: group-8 dispatch model.
//
// Groups are aligned to the ABSOLUTE row position on 8-pixel (16-byte)
// boundaries, not to the run's own start -- the row buffer starts 16-byte
// aligned and the row stride is 480 px = 960 B (16 | 960), so any run's
// interior 8-pixel-aligned span lines up with a real 128-bit-load-sized
// boundary regardless of where the run itself starts. Runs that don't
// start/end on such a boundary get a scalar prologue/epilogue, byte-
// identical to blendRow_ref's own per-pixel body (blendPixelScalar_ref).
//
// Per aligned group of 8, one decision, not eight: gather each pixel's
// colour+alpha (unavoidably scalar -- there is no gather instruction in
// this ISA, matching the shipped scrimRowPie/expandScrimInv precedent),
// then:
//   - every lane a==255        -> straight copy, no arithmetic
//   - no lane a==255            -> blendPixelGeneral_model on all eight
//                                  (safe for a==0 lanes too, per the
//                                  identity argument above)
//   - a mix of the two           -> scalar fallback for the whole group,
//                                  byte-identical to blendRow_ref's body
// The 3-way split at group granularity, not per-pixel, is what Layer 3's
// vector arithmetic can actually implement: EE.* has no per-lane branch,
// so a group either uniformly takes the vector path or it doesn't.
inline void blendRow_group8_model(uint16_t *__restrict dst, const uint8_t *__restrict colour,
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

        {
            const uint8_t *px = colour + static_cast<size_t>(x) * 3;
            for (; x < xAlignedStart; x++, px += 3) {
                blendPixelScalar_ref(dst, px, x);
            }
        }
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
                    dst[x + k] = blendPixelGeneral_model(colStage[k], dst[x + k], alphaStage[k]);
                }
            } else {
                const uint8_t *px = colour + static_cast<size_t>(x) * 3;
                for (int k = 0; k < 8; k++, px += 3) {
                    blendPixelScalar_ref(dst, px, x + k);
                }
            }
        }
        {
            const uint8_t *px = colour + static_cast<size_t>(x) * 3;
            for (; x < xEnd; x++, px += 3) {
                blendPixelScalar_ref(dst, px, x);
            }
        }
    }
}

} // namespace blendopt
