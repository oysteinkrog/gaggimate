#ifndef GAGGIMATE_SIM

// "Plasma" — the original sleep animation: palette-cycled classic plasma in
// espresso tones. Two per-column + two per-row sine terms summed per pixel,
// indexed into a rotating 256-entry palette.

#include "BgAnim.h"
#include "BgAnimClock.h"
#include "BgAnimCommon.h"
#include <string.h>

namespace {
using namespace bganim;

int16_t *colTerm = nullptr;
// colTerm plus the ordered dither, in eight copies -- one per y&7 Bayer phase.
// The dither has to vary with both x&7 and y&7, and rowTerm[y] is a single
// scalar per row, so there is nowhere else to hide it: folding it here keeps
// band()'s inner loop byte-identical to the undithered version, at the cost of
// 8*w int16 stores per frame in frame() (3840 at w=480, against 230k pixels).
//
// Eight and not four, i.e. BAYER8 rather than BAYER4, because the amplitude
// this needs is large -- plasma was the worst bander in the fleet without it --
// and a 4x4 matrix has only 16 levels to spread that amplitude over. Each step
// is then a sixteenth of the swing and the pattern reads as a weave. The 8x8
// matrix carries the same swing in 64 steps, so the individual step is four
// times smaller and the period twice as long, which is the whole difference
// between dither that dissolves a contour and dither you can see. It costs one
// more row of int16 per phase and nothing per pixel: band() indexes y&7 where
// it indexed y&3.
int16_t *colTermPh = nullptr; // read every pixel in band() -- allocHot(), see init()
int16_t *rowTerm = nullptr;   // read once per row in band() -- allocHot(), see init()
// Dither offsets in PRE-SHIFT units, where one palette index is 16, so a
// sub-index amplitude survives the >>4 in band(). Rebuilt with the wheel.
int16_t dithOff[64] = {0};
uint16_t *palette = nullptr;    // base palette (theme-cycled build)
uint16_t *rotPalette = nullptr; // palette pre-rotated by `cycle` each frame,
                                // so band() can index with a plain & 255
                                // instead of an extra per-pixel "+ cycle".
// Every slot, not just the first four: the palette follows p[2] and the
// dither follows p[6], and comparing the whole array costs one 8-byte memcmp
// per frame.
uint8_t lastP[BG_ANIM_PARAMS] = {255, 255, 255, 255, 255, 255, 255, 255}; // force first palette build
uint32_t lastThemeGen = 0xFFFFFFFF;
// Dimensions colTerm/rowTerm were sized for. release() runs after a
// resolution change too, when w/h no longer describe the allocation.
int allocW = 0, allocH = 0;
uint32_t phase1 = 0;
uint32_t phase2 = 0;
uint32_t phase3 = 0;
uint32_t cycle = 0;
// Animation time (BgAnimClock.h, gm-4q9y), at the speed setting. Not reset
// by release(), so the full/half switch keeps the drift where it was.
AnimClock g_clock;
// The colour cycle's clock: the speed setting times the colour cycle
// slider (p[4]). Not reset by release() either.
AnimClock g_cycleClock;

// Table placement (round 2, see bganim::allocHot's comment in
// BgAnimCommon.h): the hot slab is 9,216 B once the shared sinLut/cosTableF
// term is subtracted, and this animation's two per-pixel tables plus the one
// per-row table fit it with 64 B to spare, so nothing here has to be
// shrunk:
//   colTermPh   7,680 B  read every pixel in band() -- HOT
//   rotPalette    512 B  read every pixel in band(), by a data-dependent
//                        (random) index -- exactly the access pattern a
//                        PSRAM cache miss punishes hardest -- HOT
//   rowTerm       960 B  read once per ROW (480 reads/frame, not 230,400),
//                        lowest priority of the three, included only
//                        because it still fits after the two above -- HOT
//   -----------------------------------------------------------------
//                9,152 B of 9,216 B
//   colTerm       960 B  read only in frame(), never in band() -- PSRAM
//   palette       512 B  read only in frame(), never in band() -- PSRAM
// If a future change ever grows w past 480 or adds a table here, re-check
// this budget before assuming allocHot() still succeeds; a fallback to
// alloc() only costs speed (PSRAM instead of SRAM), it does not break
// correctness, since this kernel is pure scalar and needs no particular
// alignment (see plasmaRowAsm below) -- unlike round 1, nothing here
// depends on allocHot()'s 16-byte-aligned return.
// Sine-term frequency from a Q8 spatial scale, floored at 1. Scale and
// Stretch multiply, so the lowest pair (scale 0 with stretch at either end)
// would otherwise take a term to 0, and a zero-frequency sine is a constant:
// the pattern would lose one of its two terms on that axis. At the defaults
// the four frequencies are 5, 2, 4 and 3, so the floor never applies there.
inline uint32_t freqOf(uint32_t scaled) {
    const uint32_t f = scaled >> 8;
    return f < 1 ? 1u : f;
}

bool init(int w, int h) {
    if (sinLut() == nullptr) {
        return false;
    }
    if (colTerm == nullptr) {
        colTerm = static_cast<int16_t *>(alloc(w * sizeof(int16_t))); // frame()-only, PSRAM is fine
        allocW = w;
    }
    if (colTermPh == nullptr) {
        // Sized from allocW, not w, so it always matches what colTerm can hold
        // and what release() frees: if a retried init() sees colTerm already
        // allocated at an earlier width, allocW is that earlier width.
        colTermPh = static_cast<int16_t *>(allocHot(8 * allocW * sizeof(int16_t)));
    }
    if (rowTerm == nullptr) {
        rowTerm = static_cast<int16_t *>(allocHot(h * sizeof(int16_t)));
        allocH = h;
    }
    if (palette == nullptr) {
        palette = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t))); // frame()-only, PSRAM is fine
    }
    if (rotPalette == nullptr) {
        rotPalette = static_cast<uint16_t *>(allocHot(256 * sizeof(uint16_t)));
    }
    return colTerm != nullptr && colTermPh != nullptr && rowTerm != nullptr && palette != nullptr &&
           rotPalette != nullptr;
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[BG_ANIM_PARAMS]) {
    if (memcmp(p, lastP, BG_ANIM_PARAMS) != 0 || themeGen() != lastThemeGen) {
        memcpy(lastP, p, BG_ANIM_PARAMS);
        lastThemeGen = themeGen();
        const uint16_t bright = 64 + static_cast<uint16_t>(p[2]) * 192 / 100; // 25%..100%
        buildThemeWheel(palette, bright);
        // Plasma had no dither at all, and it was the worst bander in the fleet:
        // 35.6% of disc pixels on a monotone <=1 LSB staircase at brightness
        // 100, 46.0% at 55. The 0.75 is because ditherAmp() returns the MEAN
        // step spacing, and a wheel's gradient is not uniform -- the steep arcs
        // would get over-dithered into visible texture at full amplitude. At
        // 0.75 the contour share is 12.6%/9.4% with the pattern still hidden.
        // Grain scales that 0.75: none at 0, which is the bare staircase the
        // animation had before the dither, the tuned 0.75 at 50, and four
        // times that at 100, which reads as film grain over the ramp. The two
        // segments meet at exactly 0.75f, so the default output is unchanged.
        const float grain = p[6] <= 50 ? static_cast<float>(p[6]) * 0.75f / 50.0f
                                       : 0.75f + static_cast<float>(p[6] - 50) * 2.25f / 50.0f;
        const float amp = ditherAmp(palette, 256) * grain;
        for (int k = 0; k < 64; k++) {
            const float d = (static_cast<float>(BAYER8[k]) - 31.5f) * (amp * 16.0f / 31.5f);
            dithOff[k] = static_cast<int16_t>(lroundf(d));
        }
    }
    // Speed follows the fleet's curve, speedMul(): 0.15x at 0, 1x at 50 and
    // 6.7x at 100 of the drift Speed 50 has always had (bead gm-kh2s). The
    // old private law, 4 + p[0] * 44 / 100 over 16, covered 0.25x to 3x.
    // The multiplier is Q9 and 832 at 50, the old 26 over 16 with five more
    // fraction bits. Rounded, not truncated: the nearest .5 boundary over
    // Speed 0..100 is 63 float ulps away, so exp2f on the host, exp2f on the
    // device and Math.pow on the page land on the same integer.
    // The old `base = tMs * speed >> k` was a uint32 product: it wrapped
    // after about a day of uptime at full speed, and every phase jumped
    // there, on each speed change and at the millis() wrap. base now comes
    // from the wrapped-delta clock (gm-4q9y). speedQ9 / 512 is exact in the
    // clock's Q16 speed, so with a constant speed base is the same integer
    // the product gave, counted from the clock's first frame. The phases
    // are taken from the 64-bit base, so they never overflow; band() only
    // reads their low bits (& (SIN_N - 1), & 255).
    const uint32_t speedQ9 = static_cast<uint32_t>(lroundf(832.0f * speedMul(p[0])));
    const float speed = static_cast<float>(speedQ9) * (1.0f / 512.0f);
    g_clock.advance(tMs, speed);
    const uint64_t base = g_clock.simQ16 >> 16;
    phase1 = static_cast<uint32_t>(base * 30 >> 9); // ratios preserved from the frame-based original
    phase2 = static_cast<uint32_t>(base * 23 >> 9);
    phase3 = static_cast<uint32_t>(base * 26 >> 9);
    // Colour cycle: how fast the palette rotates under the pattern, apart from
    // how fast the pattern itself drifts. 0 freezes the colours in place and
    // only the shapes move; 50 is the original 11/512 of the drift; 100 is four
    // times that. The two segments meet at exactly 11.
    // The cycle has its own clock, advanced at the drift speed times
    // cycNum / 11, so moving the slider bends the rotation instead of
    // jumping the palette. At the default cycNum / 11 is exactly 1 and
    // g_cycleClock counts the same as g_clock.
    const uint32_t cycNum =
        p[4] <= 50 ? static_cast<uint32_t>(p[4]) * 11 / 50 : 11 + static_cast<uint32_t>(p[4] - 50) * 33 / 50;
    g_cycleClock.advance(tMs, speed * (static_cast<float>(cycNum) / 11.0f));
    cycle = static_cast<uint32_t>((g_cycleClock.simQ16 >> 16) * 11 >> 9);

    // Scale 0-100 -> 0.5x..2x spatial frequency.
    const uint32_t sx = 128 + static_cast<uint32_t>(p[1]) * 384 / 100; // 128..512, /256
    // Stretch tips that scale between the two axes: at 50 both gains are 256
    // and the frequencies are the original 5, 2, 4, 3; at 0 the horizontal
    // terms halve and the vertical ones grow, so the blobs pull out sideways;
    // at 100 it is the other way and they stand up tall.
    const int32_t tilt = (static_cast<int32_t>(p[5]) - 50) * 256 / 100; // -128..+128
    const uint32_t sxx = (sx * static_cast<uint32_t>(256 + tilt)) >> 8;
    const uint32_t sxy = (sx * static_cast<uint32_t>(256 - tilt)) >> 8;
    const uint32_t f5 = freqOf(5 * sxx);
    const uint32_t f2 = freqOf(2 * sxx);
    const uint32_t f4 = freqOf(4 * sxy);
    const uint32_t f3 = freqOf(3 * sxy);
    // Contrast scales both sine sums, so a pixel's index sweeps a narrower or
    // wider arc of the 256-entry wheel: half at 0, where the frame settles into
    // a few broad tones, and one and a half times at 100, where more of the
    // palette shows and the colour edges tighten. The 256 at 50 shifts a value
    // already multiplied by 256 back down by 8, which is the identity, so the
    // default column and row terms are the bytes the animation had before.
    const int32_t cgain = 128 + static_cast<int32_t>(p[3]) * 256 / 100; // 128..384, /256
    // Hoist the LUT pointer: sin1024() re-calls sinLut() (a real call8 on
    // Xtensa — the lazy-init check inside it defeats cross-TU inlining) on
    // every use, which otherwise costs 4 calls x (w+h) per frame here.
    const int16_t *sl = sinLut();
    for (int x = 0; x < w; x++) {
        const int32_t s =
            sl[(x * f5 + phase1) & (SIN_N - 1)] + sl[(x * f2 + SIN_N - (phase2 & (SIN_N - 1))) & (SIN_N - 1)];
        colTerm[x] = static_cast<int16_t>((s * cgain) >> 8);
    }
    for (int y = 0; y < h; y++) {
        const int32_t s = sl[(y * f4 + phase2) & (SIN_N - 1)] + sl[(y * f3 + phase3) & (SIN_N - 1)];
        rowTerm[y] = static_cast<int16_t>((s * cgain) >> 8);
    }
    // Expand into the eight y-phase copies. colTerm's own range is +-1024 (two
    // sine terms of amplitude 512) times contrast's 1.5 at most, so +-1536, and
    // the dither adds at most +-768 at grain 100 (ditherAmp() clamps itself to
    // 16, times grain's 3, times 16/31.5 of the Bayer swing), so +-2304 all
    // told and int16 still has ample headroom.
    for (int ph = 0; ph < 8; ph++) {
        int16_t *dstPh = colTermPh + static_cast<size_t>(ph) * w;
        const int16_t *off = &dithOff[ph * 8];
        for (int x = 0; x < w; x++) {
            dstPh[x] = static_cast<int16_t>(colTerm[x] + off[x & 7]);
        }
    }

    // Pre-rotate the palette by `cycle` once per frame so band() can index
    // with a plain `& 255` instead of paying a per-pixel "+ cycle" add.
    // rotPalette[i] == palette[(i + cycle) & 255] for all i in 0..255, which
    // is algebraically identical to the old per-pixel ((v>>4) + cycle) & 255
    // since (v>>4) & 255 already reduces v>>4 mod 256 before the rotation.
    // Palette shift slides the whole wheel under the pattern without rotating
    // it over time: nothing at 50, about 40 percent of the wheel back at 0 and
    // forward at 100, which lands the theme's bright accent on a different part
    // of the plasma. Not a hue control, whatever a rotation of a colour wheel
    // suggests: a theme is one ramp from its darkest stop to its brightest, so
    // what moves is which tone sits where, not which colours exist. A negative
    // sum wraps through the mask the way the rotation itself does.
    const int32_t shift = (static_cast<int32_t>(p[7]) - 50) * 2; // -100..+100 of 256
    const uint32_t rot = static_cast<uint32_t>(static_cast<int32_t>(cycle & 255) + shift) & 255;
    for (int i = 0; i < 256; i++) {
        rotPalette[i] = palette[(i + rot) & 255];
    }
}

// The portable spec: the original band() body, unchanged. Host bench goldens
// run against this, and the device equivalence test (SleepAnimation::
// runAnimTest, /api/debug/animtest) checks band()'s asm kernel against it
// pixel for pixel. Also the reference this pass's round 2 measured against:
// GCC 14 compiles ((ct[x]+rt)>>4)&255 to a single EXTUI (a signed right
// shift followed by an unsigned mask is the same bit pattern as one unsigned
// bitfield extract, since the mask discards every bit the two shift kinds
// could differ on -- verified against this exact function's disassembly)
// and the pairwise loop into one hardware zero-overhead LOOP, which is most
// of why round 1's PIE-vs-GCC gap was so much smaller than instruction
// counting predicted: GCC was not leaving much on the table here.
void bandRef(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int y = y0; y < y0 + rows; y++) {
        const int rt = rowTerm[y];
        // The dither is already in this row's phase copy, so the loop below is
        // unchanged from the undithered version -- zero per-pixel cost.
        const int16_t *ct = colTermPh + static_cast<size_t>(y & 7) * w;
        int x = 0;
        // Emit pixels in pairs via a single uint32 store where possible;
        // halves the number of store instructions in the hot loop. Each row
        // of dst is 32-bit aligned by band()'s contract (BgAnim.h): dst is
        // 4-byte aligned and a multi-row call has an even w, so row r at
        // r*w pixels stays aligned. An odd w (233) arrives one row per call.
        for (; x + 1 < w; x += 2) {
            const uint16_t p0 = rotPalette[((ct[x] + rt) >> 4) & 255];
            const uint16_t p1 = rotPalette[((ct[x + 1] + rt) >> 4) & 255];
            *reinterpret_cast<uint32_t *>(dst) = static_cast<uint32_t>(p0) | (static_cast<uint32_t>(p1) << 16);
            dst += 2;
        }
        for (; x < w; x++) {
            *dst++ = rotPalette[((ct[x] + rt) >> 4) & 255];
        }
    }
}

#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
// Round 1 of this pass tried PIE: a vector kernel computed
// ((ct[i]+rt)>>4)&255 for a whole row into a scratch buffer, then a scalar
// kernel gathered from it. Bit-exact and it assembled clean, but on the
// device it only matched bandRef() (0.98x in SRAM, i.e. very slightly
// slower) instead of beating it. The reason shows up in bandRef()'s own
// disassembly (tools/animbench/xtensa-asm14.sh AnimPlasma): GCC already
// compiles the shift-and-mask into a single EXTUI, and the pairwise loop
// into a hardware zero-overhead LOOP with only one unhidden load-use stall
// (ct[x+1] consumed the instruction right after its own load). PIE's real
// saving over one EXTUI per pixel is small (0.25 vector instr/pixel for the
// shift+mask against one scalar instruction), and round 1's scratch buffer
// spent more than that saving right back: a store per pixel out of the
// vector kernel and a load per pixel back into the gather kernel, memory
// traffic bandRef() never pays because it keeps the index in a register
// from computation to use. Splitting index math and gather into two
// __attribute__((noinline)) functions also paid two windowed-ABI calls a
// row instead of bandRef's zero.
//
// This round fuses index math and gather into one hand-scheduled scalar
// kernel, matching bandRef's shape but fixing the one stall its
// disassembly showed and widening the interleave from bandRef's 2 pixels
// to 4 (the same interleave AnimStarfield.cpp's starfieldVigRowAsm uses for
// its own gather) so every load has an unrelated instruction between it and
// its own use. No PIE at all this round -- see plasmaRowAsm below for why
// EXTUI already gets the shift+mask in one instruction, which removed the
// PIE unit's only real advantage here. This also drops CPENABLE out of the
// picture entirely: a kernel that never issues an EE.* instruction never
// triggers the lazy coprocessor-enable trap PIE requires.
//
// idx = ((ct[i]+rt)>>4)&255: EXTUI extracts bits [4,12) of the 32-bit
// register unsigned (logical shift, not arithmetic), but bits [4,12) of a
// value are identical whether the shift that got them there was logical or
// arithmetic -- the two only differ in the sign-extension bits above bit
// 31-4=27, which this extraction never reaches (verified against bandRef's
// own GCC-generated EXTUI, not reinvented). ct[i]+rt never approaches
// int16/int32 overflow either way: colTerm's two sine terms are each +-512
// (SIN_AMP), so colTerm and rowTerm are each in [-1024, 1024]; the dither
// folded into colTermPh is capped at ditherAmp()*0.75*16/31.5*31.5 --
// ditherAmp() itself clamps to 16.0f, so |dithOff| <= 192 pre-round. So
// ct[i] is in [-1216, 1216] and ct[i]+rt is in [-2240, 2240], nowhere near
// any width this arithmetic uses. Checked at every param extreme: p[0]
// (speed), p[1] (scale) and p[5] (stretch) only change phase and frequency,
// not amplitude; p[4] (colour cycle) and p[7] (palette shift) only move the
// palette rotation; p[2] (brightness) feeds buildThemeWheel and ditherAmp,
// and ditherAmp's own 16.0f clamp already bounds the dither term regardless
// of brightness. Two params do widen the sum and frame() bounds both: p[3]
// (contrast) scales each sine sum by at most 1.5, so |ct| <= 1536 plus 768
// of dither and |rt| <= 1536, and p[6] (grain) is that 768, capped by the
// same ditherAmp clamp times grain's own ceiling of 3.0. So ct[i] is in
// [-2304, 2304] and ct[i]+rt in [-3840, 3840], still nowhere near any width
// this arithmetic uses, and EXTUI's bits [4,12) are reached the same way.
//
// dst and ct need only their natural 4-byte/2-byte alignment (S32I/L16SI
// have no wider requirement) -- unlike round 1's PIE kernel, nothing here
// depends on allocHot()'s 16-byte-aligned return. The S32I stores need a
// 4-byte-aligned dst and cover four pixels each, so band() below writes a
// pixel before the kernel when a row starts 2 mod 4 and the w & 3 pixels
// after it in scalar (gm-bzu.49): at 466 px the old w >> 2 call left the
// last two columns unwritten, at 233 the last one.
//
// Register budget: ctp, dstp, rt, pal, n (5, live for the whole loop) +
// t1..t4 (4, reused in place for ct value then address then palette value,
// same trick Starfield's kernel uses) = 9, comfortably under the
// ~13-usable-AR ceiling ASM_BRIEF.md documents.
__attribute__((noinline)) static void plasmaRowAsm(uint16_t *__restrict dst, const int16_t *__restrict ct, int rt,
                                                    const uint16_t *__restrict pal, int nQuad) {
    const int16_t *ctp = ct;
    uint16_t *dstp = dst;
    int32_t t1, t2, t3, t4; // scratch; values unused after the block
    asm volatile("loopnez %[n], 2f\n"
                 "l16si   %[t1], %[ctp], 0\n"     // ct[0], sign-extending (ct is signed)
                 "l16si   %[t2], %[ctp], 2\n"     // ct[1]
                 "l16si   %[t3], %[ctp], 4\n"     // ct[2]
                 "l16si   %[t4], %[ctp], 6\n"     // ct[3]
                 "add     %[t1], %[t1], %[rt]\n"  // ct[0] + rt
                 "add     %[t2], %[t2], %[rt]\n"  // ct[1] + rt
                 "add     %[t3], %[t3], %[rt]\n"  // ct[2] + rt
                 "add     %[t4], %[t4], %[rt]\n"  // ct[3] + rt
                 "extui   %[t1], %[t1], 4, 8\n"   // idx0 = (v>>4)&255, one instruction
                 "extui   %[t2], %[t2], 4, 8\n"   // idx1
                 "extui   %[t3], %[t3], 4, 8\n"   // idx2
                 "extui   %[t4], %[t4], 4, 8\n"   // idx3
                 "addx2   %[t1], %[t1], %[pal]\n" // &pal[idx0]
                 "addx2   %[t2], %[t2], %[pal]\n" // &pal[idx1]
                 "l16ui   %[t1], %[t1], 0\n"      // pal[idx0]
                 "addx2   %[t3], %[t3], %[pal]\n" // &pal[idx2]
                 "l16ui   %[t2], %[t2], 0\n"      // pal[idx1]
                 "addx2   %[t4], %[t4], %[pal]\n" // &pal[idx3]
                 "l16ui   %[t3], %[t3], 0\n"      // pal[idx2]
                 "l16ui   %[t4], %[t4], 0\n"      // pal[idx3]
                 "slli    %[t2], %[t2], 16\n"
                 "or      %[t1], %[t1], %[t2]\n" // pixels 0,1 packed
                 "slli    %[t4], %[t4], 16\n"
                 "or      %[t3], %[t3], %[t4]\n" // pixels 2,3 packed
                 "s32i    %[t1], %[dstp], 0\n"
                 "s32i    %[t3], %[dstp], 4\n"
                 "addi    %[ctp], %[ctp], 8\n"   // four int16_t
                 "addi    %[dstp], %[dstp], 8\n" // four uint16_t
                 "2:\n"
                 : [ctp] "+r"(ctp), [dstp] "+r"(dstp), [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3),
                   [t4] "=&r"(t4)
                 : [rt] "r"(rt), [pal] "r"(pal), [n] "r"(nQuad)
                 : "memory");
}
#endif

// Device path: one fused scalar kernel per row. Host / non-Xtensa builds:
// band() IS bandRef(), not merely equivalent to it -- there is no second
// scalar path to keep in sync.
void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
    for (int r = 0; r < rows; r++) {
        const int y = y0 + r;
        const int rt = rowTerm[y];
        const int16_t *ct = colTermPh + static_cast<size_t>(y & 7) * w;
        uint16_t *row = dst + static_cast<size_t>(r) * w;
        // Same per-pixel formula as bandRef, one pixel at a time.
        auto px = [&](int x) { row[x] = rotPalette[((ct[x] + rt) >> 4) & 255]; };
        int x = 0;
        if ((reinterpret_cast<uintptr_t>(row) & 3) != 0 && w > 0) {
            px(0);
            x = 1;
        }
        const int nQuad = (w - x) >> 2;
        plasmaRowAsm(row + x, ct + x, rt, rotPalette, nQuad);
        for (x += nQuad * 4; x < w; x++) {
            px(x);
        }
    }
#else
    bandRef(dst, y0, rows, w, tMs, p);
#endif
}

void release() {
    releaseTable(colTerm, static_cast<size_t>(allocW) * sizeof(int16_t));
    releaseTable(colTermPh, static_cast<size_t>(8 * allocW) * sizeof(int16_t));
    releaseTable(rowTerm, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(palette, 256 * sizeof(uint16_t));
    releaseTable(rotPalette, 256 * sizeof(uint16_t));
    allocW = allocH = 0;
    lastThemeGen = 0xFFFFFFFF;
}

} // namespace

// extern: const namespace-scope objects default to internal linkage.
extern const BgAnimation bg_anim_plasma;
const BgAnimation bg_anim_plasma = {
    "plasma",
    "Plasma",
    {{"speed", "Speed", 50},
     {"scale", "Scale", 50},
     {"brightness", "Brightness", 70},
     {"contrast", "Contrast", 50},
     {"cycle", "Colour cycle", 50},
     {"stretch", "Stretch", 50},
     {"grain", "Grain", 50},
     {"shift", "Palette shift", 50}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
