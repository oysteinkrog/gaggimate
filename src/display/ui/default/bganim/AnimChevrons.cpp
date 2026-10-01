#ifndef GAGGIMATE_SIM

// "Chevrons": soft, rounded V folds with a lit face and a shaded face. A
// wide sine swell travels across them while their slant opens and closes.
// This ports entry 17 in tools/animbench/web/anim_bench.html, including its
// integer clocks and theme mapping. The page header describes an earlier
// tuning: its code gives three folds at default spacing and a crest at
// ramp index 121, not five folds and index 195.
//
// The separable field is Q8.4 palette phase, wrapped at 4096 sum units:
//   index = ((colTermPh[(y & 7) * w + x] + rowTerm[y]) >> 4) & 255.
// Both terms are 0..4095 before band(), so the sum is 0..8190. Dropping
// multiples of 4096 is exact because the final index wraps at 256. Eight
// column copies bake in the page's 8x8 Bayer offsets before the shift.
// Every row selects its phase using absolute y, including single-row and
// parity-skipping calls. No state is carried between band calls.
// Host PPMs expand RGB565 by integer scaling; the page replicates channel
// bits. Their RGB888 values can differ by one with identical RGB565 pixels.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <string.h>

// Enabled on the device by default. Host builds and an explicit NO_ASM or
// flag value of zero use bandRef. Silicon parity and production timing
// are still required before claiming a device speedup.
#ifndef GM_BGANIM_CHEVRONS_ASM
#define GM_BGANIM_CHEVRONS_ASM 1
#endif

namespace {
using namespace bganim;

constexpr int WRAP = 4096; // 256 palette entries with four fractional bits
constexpr int PHASES = 8;  // one column copy for each Bayer row

int32_t *colTerm = nullptr;    // page's Int32 column workspace, frame only
int16_t *colTermPh = nullptr;  // PHASES * w, read every pixel
int16_t *rowTerm = nullptr;    // h, read once per row
uint16_t *themeRamp = nullptr; // 256, palette rebuild only
uint16_t *palette = nullptr;   // 256, per-pixel gather
int16_t *dithOff = nullptr;    // 64, frame only
const int16_t *sine = nullptr; // borrowed shared sinLut, never freed here

uint8_t lastP[BG_ANIM_PARAMS] = {255, 255, 255, 255, 255, 255, 255, 255};
uint32_t lastThemeGen = 0xFFFFFFFFu;
bool paletteValid = false;
int allocW = 0, allocH = 0;

// At 480x480, against the 9,216 B per-animation slab:
//   colTermPh  7,680 B  slab, eight phases of 480 int16 values
//   rowTerm      960 B  slab, 480 int16 values
//   palette      512 B  slab, 256 RGB565 values
//              9,152 B  total, including 16-byte allocation rounding
//   colTerm    1,920 B  PSRAM, 480 int32 values
//   themeRamp    512 B  PSRAM, 256 RGB565 values
//   dithOff      128 B  PSRAM, 64 int16 values
// The shared sine is covered by BgAnimCommon's shared reserve. At smaller
// widths phase starts need not be 16-byte aligned; the kernel handles that
// explicitly without padding the page's table layout.
void release();

bool init(int w, int h) {
    if (w <= 0 || h <= 0) {
        release();
        return false;
    }
    if (w != allocW || h != allocH) {
        release();
    }
    if (palette != nullptr) {
        return true; // only a complete allocation set survives init
    }
    allocW = w;
    allocH = h;
    sine = sinLut();
    colTerm = static_cast<int32_t *>(alloc(static_cast<size_t>(w) * sizeof(int32_t)));
    colTermPh = static_cast<int16_t *>(allocHot(PHASES * static_cast<size_t>(w) * sizeof(int16_t)));
    rowTerm = static_cast<int16_t *>(allocHot(static_cast<size_t>(h) * sizeof(int16_t)));
    themeRamp = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
    dithOff = static_cast<int16_t *>(alloc(64 * sizeof(int16_t)));
    palette = static_cast<uint16_t *>(allocHot(256 * sizeof(uint16_t)));
    if (!sine || !colTerm || !colTermPh || !rowTerm || !themeRamp || !dithOff || !palette) {
        // Return all earlier hot allocations after a PSRAM failure too,
        // so retry starts with an empty slab (gm-bzu.15).
        release();
        return false;
    }
    return true;
}

void rebuildPalette(int contrastP, int highlightP) {
    buildThemeRamp(themeRamp, 256);
    const int peak = 24 + contrastP * 30 / 100; // 24..54, 34 at default 35
    // Highlight scales the cubed crest term: 0 at 0 for a plain raised
    // cosine, 80 at 50 (the constant this file used before the slider),
    // 160 at 100 for a hot, narrow lit edge on every fold.
    const int hi = highlightP * 160 / 100;
    for (int i = 0; i < 256; i++) {
        // Four sine slots per entry and +768 start at the trough. Raised
        // cosine s is Q9 in 0..512. Round after each multiply as the page
        // does, giving the cubed term's soft lit crest.
        const int s = (sine[(i * 4 + 768) & (SIN_N - 1)] + 512) >> 1;
        const int s2 = (s * s) >> 9;
        const int s3 = (s2 * s) >> 9;
        const int v = 7 + ((s * peak) >> 9) + ((s3 * hi) >> 9);
        // Floor 7, peak 54 and highlight 160 bound v to 7..221 over every
        // parameter value, so the page's 0..255 clamp is redundant.
        palette[i] = themeRamp[v];
    }
    // Three quarters of the measured RGB565 step spacing, converted to
    // Q8.4 phase units. ditherAmp caps at 16, so offsets stay in +/-192.
    // lroundf matches the page's lround (halves away from zero). Float
    // rounding may differ by one phase unit at a boundary across hosts;
    // the per-pixel fixed point and device kernel are otherwise exact.
    const float amp = ditherAmp(palette, 256) * 0.75f;
    for (int k = 0; k < 64; k++) {
        const float d = (static_cast<float>(BAYER8[k]) - 31.5f) * (amp * 16.0f / 31.5f);
        dithOff[k] = static_cast<int16_t>(lroundf(d));
    }
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[BG_ANIM_PARAMS]) {
    const uint32_t generation = themeGen();
    if (!paletteValid || lastP[3] != p[3] || lastP[7] != p[7] || generation != lastThemeGen) {
        rebuildPalette(p[3], p[7]);
        lastThemeGen = generation;
        memcpy(lastP, p, BG_ANIM_PARAMS);
        paletteValid = true;
    }

    const int denom = h > 0 ? h : 1;
    const int folds = 2 + static_cast<int>(p[1]) * 3 / 100; // 2..5, three at 65
    const int rowStep = folds * WRAP / denom;
    // Speed follows the fleet's curve, speedMul(): 0.15x at 0, 1x at 50 and
    // 6.7x at 100 of the rate Speed 50 has since gm-33fm (bead gm-kh2s).
    // The multiplier is Q9 and 3456 at 50, the old sp4 of 27 quarters with
    // seven more fraction bits, and the three shifts below take them back,
    // so Speed 50 is the same picture: the phase advances 27000/256 sum
    // units per second, 16.9 px/s at defaults on a 480-high panel (rowStep
    // == 25). The old affine law read 4 + p[0] * 46 / 100 quarters and
    // covered 0.15x to 1.85x. Preserve every JS >>>0 before shifting: both
    // products wrap as uint32, including base*3, and the phases read bits
    // 13..29 of them at most, so the wrap never shows. Do not substitute an
    // elapsed time accumulator. Rounded, not truncated: the nearest .5
    // boundary over Speed 0..100 is 22 float ulps away, so the host, the
    // device and the page agree.
    const uint32_t speedQ9 = static_cast<uint32_t>(lroundf(3456.0f * speedMul(p[0])));
    const uint32_t base = tMs * speedQ9;
    const uint32_t phase = (base >> 13) & (WRAP - 1);
    const uint32_t phW = base >> 19;
    const uint32_t phB = (base * 3u) >> 20;
    // Q8 slant 110..290, modulated by 256 +/-30. At default speed the
    // wobble period is 155.3446 s and the swell period 103.5631 s, as the
    // page computes, despite its older approximate 30 s comment.
    const int angleQ8 = 110 + static_cast<int>(p[2]) * 180 / 100;
    const int wob = 256 + ((sine[phW & (SIN_N - 1)] * 30) >> 9);
    const int colStep = (((rowStep * angleQ8) >> 8) * wob) >> 8;
    const int cx = w / 2;
    // Roundness: the apex radius as a share of width, 0 for a sharp V,
    // 22 percent at 50 (the old constant), 44 percent at 100.
    const int roundPct = static_cast<int>(p[4]) * 44 / 100;
    const int round = w * roundPct / 100;
    const int halfRound = round >> 1;
    // Swell depth: amplitude of the travelling bright zone in sum units,
    // flat at 0, 760 at 50 (the old constant), 1520 at 100.
    const int swellAmp = static_cast<int>(p[5]) * 760 / 50;
    // Swell width: sine slots per column in sixteenths, 8 at 0 for one
    // broad zone, 23 at 50 (the old constant), 38 at 100 for tight bands.
    const int swellK = 8 + static_cast<int>(p[6]) * 30 / 100;
    for (int x = 0; x < w; x++) {
        const int dx = x - cx;
        int adx = dx < 0 ? -dx : dx;
        // Parabola meets the outer absolute-distance slope at round. For
        // widths below five round is zero and the first arm always wins,
        // so division by zero is never evaluated, just as on the page.
        adx = adx >= round ? adx - halfRound : adx * adx / (2 * round);
        // swellK/16 sine slots per column (23/16 at default), swellAmp sum
        // units of swell (760 at default), unchanged in pixel units at half
        // resolution. Signed >>9 rounds down on both supported compilers,
        // matching JavaScript's arithmetic >>.
        const uint32_t idx = (static_cast<uint32_t>(x * swellK) >> 4) - phB;
        const int swell = (sine[idx & (SIN_N - 1)] * swellAmp) >> 9;
        colTerm[x] = static_cast<int32_t>(static_cast<uint32_t>(adx * colStep + swell) & (WRAP - 1));
    }
    for (int ph = 0; ph < PHASES; ph++) {
        int16_t *ct = colTermPh + static_cast<size_t>(ph) * w;
        const int16_t *off = dithOff + ph * 8;
        for (int x = 0; x < w; x++) {
            // Mask is the page's signed remainder then add WRAP if
            // negative, with no signed-overflow risk.
            ct[x] = static_cast<int16_t>(static_cast<uint32_t>(colTerm[x] + off[x & 7]) & (WRAP - 1));
        }
    }
    for (int y = 0; y < h; y++) {
        rowTerm[y] = static_cast<int16_t>((static_cast<uint32_t>(y) * rowStep + phase) & (WRAP - 1));
    }
}

GM_ANIM_IRAM void bandRef(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int y = y0; y < y0 + rows; y++) {
        const int rt = rowTerm[y];
        const int16_t *ct = colTermPh + static_cast<size_t>(y & 7) * w;
        int x = 0;
        for (; x + 1 < w; x += 2) {
            const uint16_t c0 = palette[((ct[x] + rt) >> 4) & 255];
            const uint16_t c1 = palette[((ct[x + 1] + rt) >> 4) & 255];
            *reinterpret_cast<uint32_t *>(dst) = static_cast<uint32_t>(c0) | (static_cast<uint32_t>(c1) << 16);
            dst += 2;
        }
        for (; x < w; x++) {
            *dst++ = palette[((ct[x] + rt) >> 4) & 255];
        }
    }
}

#if GM_BGANIM_CHEVRONS_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
// GCC 14.2's bandRef .L4 loop, read before this kernel was written, is
// 15 instructions per pair with no immediate load-use stalls. The scalar
// loop below transcribes that schedule. PIE takes the additional edge:
// one load and one saturating add form eight sums, then EE.MOVI.32.A
// extracts each pair directly into an AR. EXTUI selects bits 4..11 and
// 20..27, implementing the page's shift and wrap without scratch stores.
// There is no vector palette gather, so those reads remain scalar.
//
// Vector loop: 44 instructions per eight pixels, 5.5 per pixel, versus
// GCC's 7.5. Updating n between VLD and VADDS fills the vector load-use
// gap; paired palette loads are also separated from their consumers.
// No loop-body spills, no per-pixel multiply, no PSRAM gather. A 5.5
// cycles/pixel issue floor assumes hot data and code; it is not a measured
// device cost. Host timings cannot predict PIE context or flash costs.
//
// ct values and rt are 0..4095, so signed saturation is never reached
// (sum <=8190). dst is 4-byte aligned by BgAnim.h. A scalar pair prefix
// aligns ct to 16 bytes while preserving dst alignment. At odd widths a
// phase can start at 2 mod 4; that row uses the scalar pair loop throughout
// because aligning ct would misalign dst. No vector load reads padding.
//
// GCC never allocates q registers, so q0/q1 have no clobber syntax. There
// are no C calls between their definition and use. FreeRTOS owns lazy
// coprocessor context: this kernel never writes CPENABLE or changes SAR.
// Plain pointers and ints let the complete function be copied verbatim
// into tools/qemubench/tests/anim_chevrons/main.c.
GM_ANIM_IRAM __attribute__((noinline)) void chevronsRowAsm(uint16_t *out, const int16_t *ct,
                                                        const uint16_t *pal, int rt, int n) {
    if (n <= 0) {
        return;
    }
    if (((uintptr_t)ct & 3u) == 0) {
        for (; n >= 2 && ((uintptr_t)ct & 15u) != 0; n -= 2) {
            const uint32_t c0 = pal[((ct[0] + rt) >> 4) & 255];
            const uint32_t c1 = pal[((ct[1] + rt) >> 4) & 255];
            *(uint32_t *)out = c0 | (c1 << 16);
            ct += 2;
            out += 2;
        }
        const int blocks = n >> 3;
        if (blocks != 0) {
            const uint32_t packedRow = (uint32_t)rt | ((uint32_t)rt << 16);
            uint32_t sum, c0, c1;
            asm volatile("ee.movi.32.q q1, %[row], 0\n"
                         "ee.movi.32.q q1, %[row], 1\n"
                         "ee.movi.32.q q1, %[row], 2\n"
                         "ee.movi.32.q q1, %[row], 3\n"
                         "loopnez %[blocks], 1f\n"
                         "ee.vld.128.ip q0, %[ct], 16\n"
                         "addi %[n], %[n], -8\n"
                         "ee.vadds.s16 q0, q0, q1\n"
                         "ee.movi.32.a q0, %[sum], 0\n"
                         "extui %[c0], %[sum], 4, 8\n"
                         "extui %[c1], %[sum], 20, 8\n"
                         "addx2 %[c0], %[c0], %[pal]\n"
                         "addx2 %[c1], %[c1], %[pal]\n"
                         "l16ui %[c1], %[c1], 0\n"
                         "l16ui %[c0], %[c0], 0\n"
                         "slli %[c1], %[c1], 16\n"
                         "or %[c0], %[c0], %[c1]\n"
                         "s32i %[c0], %[out], 0\n"
                         "ee.movi.32.a q0, %[sum], 1\n"
                         "extui %[c0], %[sum], 4, 8\n"
                         "extui %[c1], %[sum], 20, 8\n"
                         "addx2 %[c0], %[c0], %[pal]\n"
                         "addx2 %[c1], %[c1], %[pal]\n"
                         "l16ui %[c1], %[c1], 0\n"
                         "l16ui %[c0], %[c0], 0\n"
                         "slli %[c1], %[c1], 16\n"
                         "or %[c0], %[c0], %[c1]\n"
                         "s32i %[c0], %[out], 4\n"
                         "ee.movi.32.a q0, %[sum], 2\n"
                         "extui %[c0], %[sum], 4, 8\n"
                         "extui %[c1], %[sum], 20, 8\n"
                         "addx2 %[c0], %[c0], %[pal]\n"
                         "addx2 %[c1], %[c1], %[pal]\n"
                         "l16ui %[c1], %[c1], 0\n"
                         "l16ui %[c0], %[c0], 0\n"
                         "slli %[c1], %[c1], 16\n"
                         "or %[c0], %[c0], %[c1]\n"
                         "s32i %[c0], %[out], 8\n"
                         "ee.movi.32.a q0, %[sum], 3\n"
                         "extui %[c0], %[sum], 4, 8\n"
                         "extui %[c1], %[sum], 20, 8\n"
                         "addx2 %[c0], %[c0], %[pal]\n"
                         "addx2 %[c1], %[c1], %[pal]\n"
                         "l16ui %[c1], %[c1], 0\n"
                         "l16ui %[c0], %[c0], 0\n"
                         "slli %[c1], %[c1], 16\n"
                         "or %[c0], %[c0], %[c1]\n"
                         "s32i %[c0], %[out], 12\n"
                         "addi %[out], %[out], 16\n"
                         "1:\n"
                         : [ct] "+&r"(ct), [out] "+&r"(out), [n] "+&r"(n),
                           [sum] "=&r"(sum), [c0] "=&r"(c0), [c1] "=&r"(c1)
                         : [row] "r"(packedRow), [pal] "r"(pal), [blocks] "r"(blocks)
                         : "memory");
        }
    }
    // GCC's original schedule for the 0..3 tail pairs, or the whole row
    // when ct is at 2 mod 4. Both pointers walk, and LOOPNEZ covers zero.
    const int pairs = n >> 1;
    uint32_t c0, c1;
    asm volatile("loopnez %[pairs], 2f\n"
                 "l16si %[c1], %[ct], 2\n"
                 "l16si %[c0], %[ct], 0\n"
                 "add %[c1], %[c1], %[rt]\n"
                 "extui %[c1], %[c1], 4, 8\n"
                 "add %[c0], %[c0], %[rt]\n"
                 "addx2 %[c1], %[c1], %[pal]\n"
                 "extui %[c0], %[c0], 4, 8\n"
                 "l16ui %[c1], %[c1], 0\n"
                 "addx2 %[c0], %[c0], %[pal]\n"
                 "l16ui %[c0], %[c0], 0\n"
                 "slli %[c1], %[c1], 16\n"
                 "or %[c1], %[c1], %[c0]\n"
                 "s32i %[c1], %[out], 0\n"
                 "addi %[ct], %[ct], 4\n"
                 "addi %[out], %[out], 4\n"
                 "2:\n"
                 : [ct] "+&r"(ct), [out] "+&r"(out), [c0] "=&r"(c0), [c1] "=&r"(c1)
                 : [rt] "r"(rt), [pal] "r"(pal), [pairs] "r"(pairs)
                 : "memory");
    if ((n & 1) != 0) {
        *out = pal[((ct[0] + rt) >> 4) & 255];
    }
}
#endif

GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
#if GM_BGANIM_CHEVRONS_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
    for (int y = y0; y < y0 + rows; y++) {
        chevronsRowAsm(dst, colTermPh + static_cast<size_t>(y & 7) * w, palette, rowTerm[y], w);
        dst += w;
    }
#else
    bandRef(dst, y0, rows, w, tMs, p);
#endif
}

void release() {
    releaseTable(colTerm, static_cast<size_t>(allocW) * sizeof(int32_t));
    releaseTable(colTermPh, PHASES * static_cast<size_t>(allocW) * sizeof(int16_t));
    releaseTable(rowTerm, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(themeRamp, 256 * sizeof(uint16_t));
    releaseTable(dithOff, 64 * sizeof(int16_t));
    releaseTable(palette, 256 * sizeof(uint16_t));
    sine = nullptr;
    allocW = allocH = 0;
    paletteValid = false;
    lastThemeGen = 0xFFFFFFFFu;
    memset(lastP, 255, sizeof(lastP));
}

} // namespace

extern const BgAnimation bg_anim_chevrons;
const BgAnimation bg_anim_chevrons = {
    "chevrons",
    "Chevrons",
    {{"speed", "Speed", 50},
     {"spacing", "Spacing", 65},
     {"angle", "Angle", 50},
     {"contrast", "Contrast", 35},
     {"round", "Roundness", 50},
     {"swell", "Swell depth", 50},
     {"swellw", "Swell width", 50},
     {"highlight", "Highlight", 50}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
