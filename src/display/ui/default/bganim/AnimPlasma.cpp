#ifndef GAGGIMATE_SIM

// "Plasma" — the original sleep animation: palette-cycled classic plasma in
// espresso tones. Two per-column + two per-row sine terms summed per pixel,
// indexed into a rotating 256-entry palette.

#include "BgAnim.h"
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
uint8_t lastP[4] = {255, 255, 255, 255}; // force first palette build
uint32_t lastThemeGen = 0xFFFFFFFF;
// Dimensions colTerm/rowTerm were sized for. release() runs after a
// resolution change too, when w/h no longer describe the allocation.
int allocW = 0, allocH = 0;
uint32_t phase1 = 0;
uint32_t phase2 = 0;
uint32_t phase3 = 0;
uint32_t cycle = 0;

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

void frame(uint32_t tMs, int w, int h, const uint8_t p[4]) {
    if (memcmp(p, lastP, 4) != 0 || themeGen() != lastThemeGen) {
        memcpy(lastP, p, 4);
        lastThemeGen = themeGen();
        const uint16_t bright = 64 + static_cast<uint16_t>(p[2]) * 192 / 100; // 25%..100%
        buildThemeWheel(palette, bright);
        // Plasma had no dither at all, and it was the worst bander in the fleet:
        // 35.6% of disc pixels on a monotone <=1 LSB staircase at brightness
        // 100, 46.0% at 55. The 0.75 is because ditherAmp() returns the MEAN
        // step spacing, and a wheel's gradient is not uniform -- the steep arcs
        // would get over-dithered into visible texture at full amplitude. At
        // 0.75 the contour share is 12.6%/9.4% with the pattern still hidden.
        const float amp = ditherAmp(palette, 256) * 0.75f;
        for (int k = 0; k < 64; k++) {
            const float d = (static_cast<float>(BAYER8[k]) - 31.5f) * (amp * 16.0f / 31.5f);
            dithOff[k] = static_cast<int16_t>(lroundf(d));
        }
    }
    // Speed 0-100 -> 0.25x..3x of the original drift (which advanced ~60
    // sine-index units per second on the fastest term).
    const uint32_t speedMul = 4 + static_cast<uint32_t>(p[0]) * 44 / 100; // 4..48, /16 = 0.25..3
    const uint32_t base = tMs * speedMul >> 4;                            // ~= original frame*2 at 50
    phase1 = base * 30 >> 9; // ratios preserved from the frame-based original
    phase2 = base * 23 >> 9;
    phase3 = base * 26 >> 9;
    cycle = base * 11 >> 9;

    // Scale 0-100 -> 0.5x..2x spatial frequency.
    const uint32_t sx = 128 + static_cast<uint32_t>(p[1]) * 384 / 100; // 128..512, /256
    const uint32_t f5 = (5 * sx) >> 8;
    const uint32_t f2 = (2 * sx) >> 8; // sx >= 128 keeps every frequency >= 1
    const uint32_t f4 = (4 * sx) >> 8;
    const uint32_t f3 = (3 * sx) >> 8;
    // Hoist the LUT pointer: sin1024() re-calls sinLut() (a real call8 on
    // Xtensa — the lazy-init check inside it defeats cross-TU inlining) on
    // every use, which otherwise costs 4 calls x (w+h) per frame here.
    const int16_t *sl = sinLut();
    for (int x = 0; x < w; x++) {
        colTerm[x] = sl[(x * f5 + phase1) & (SIN_N - 1)] + sl[(x * f2 + SIN_N - (phase2 & (SIN_N - 1))) & (SIN_N - 1)];
    }
    for (int y = 0; y < h; y++) {
        rowTerm[y] = sl[(y * f4 + phase2) & (SIN_N - 1)] + sl[(y * f3 + phase3) & (SIN_N - 1)];
    }
    // Expand into the eight y-phase copies. colTerm's own range is +-1024 (two
    // sine terms of amplitude 512) and the dither adds at most +-256, so int16
    // still has headroom.
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
    const uint32_t rot = cycle & 255;
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
        // Emit pixels in pairs via a single uint32 store where possible —
        // halves the number of store instructions in the hot loop (device
        // has no unaligned-16 penalty here since dst is always 32-bit
        // aligned: bands start at a row boundary and w is even (480)).
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
// any width this arithmetic uses. Checked at both param extremes: p[0]
// (speed) and p[1] (scale) only change phase/frequency, not amplitude;
// p[2] (brightness) feeds buildThemeWheel and ditherAmp, and ditherAmp's
// own 16.0f clamp already bounds the dither term regardless of brightness.
//
// dst and ct need only their natural 4-byte/2-byte alignment (S32I/L16SI
// have no wider requirement) -- unlike round 1's PIE kernel, nothing here
// depends on allocHot()'s 16-byte-aligned return. w must be a multiple of 4
// for nQuad = w/4 to be exact; w is always a multiple of 16 on this panel
// (see CLAUDE.md), so band() never has a remainder to special-case.
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
        const int16_t *ct = colTermPh + static_cast<size_t>(y & 7) * w;
        plasmaRowAsm(dst + static_cast<size_t>(r) * w, ct, rowTerm[y], rotPalette, w >> 2);
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
    {{"speed", "Speed", 50}, {"scale", "Scale", 50}, {"brightness", "Brightness", 70}, {nullptr, nullptr, 0}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
