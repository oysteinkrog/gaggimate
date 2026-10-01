#ifndef GAGGIMATE_SIM

// "Saddle" - soft tonal contours flowing along hyperbolas, with a wandering
// centre and breathing curvature. This implements entry 19 in
// tools/animbench/web/anim_bench.html, including its wrapping cosine palette.
//
// The index combine is a multiply rather than an add, which is the whole idea:
//
//   index = (((colTermPh[x] * rowTerm[y] + 131072) >> 10) + phase) & 255
//
// The level sets of a product of two one-dimensional profiles are hyperbolas,
// so the tonal boundaries are
// curved even though both tables are straight lines of numbers. What is not
// curved is the zero set, which is one vertical and one horizontal line.
// Each profile has one crossing and a wide shoulder. The cyclic palette and
// advancing phase move its contours through that saddle continuously.
//
// The bias of 131072 is 128 << 10, so the expression is exactly
// 128 + ((c * r) >> 10) without ever right-shifting a negative value. Both
// forms round the same way, toward minus infinity, so this is a rewrite and
// not an approximation; it keeps the C++ reference clear of a shift whose
// behaviour on negative operands C++17 leaves to the implementation.
//
// The dither folded into the column copies is multiplied by the row value
// along with everything else, and that is the right behaviour rather than a
// compromise. A row with a small row value spans a small part of the palette,
// so its gradient is shallow, its contours are far apart and it needs
// proportionally less dither, which is exactly what the multiply gives it.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <string.h>

#ifndef GM_BGANIM_SADDLE_ASM
#define GM_BGANIM_SADDLE_ASM 1
#endif

namespace {
using namespace bganim;

// Ranges. The product of the two caps is 129,032, which the shift takes to
// 126, so the index before the contour phase lands in 1..254. The column term
// carries eight times the resolution of the row term so the dither, which
// lives in column units, still has somewhere to go.
constexpr int COL_MAX = 1016;
constexpr int ROW_MAX = 127;
constexpr int BIAS = 128 << 10;
static_assert(COL_MAX * ROW_MAX < BIAS, "positive bias must cover the signed product");
static_assert(8 * 480 * sizeof(int16_t) + 480 * sizeof(int16_t) + 256 * sizeof(uint16_t) +
                  32 * sizeof(int16_t) <= HOT_SLAB_BYTES - HOT_SHARED_RESERVE,
              "Saddle tables must fit the per-animation hot slab");

int16_t *colTerm = nullptr;   // frame() only -> PSRAM
int16_t *colTermPh = nullptr; // read every pixel -> slab, 8 y-phase copies
int16_t *rowTerm = nullptr;   // read once per row -> slab
uint16_t *themeRamp = nullptr; // frame() only -> PSRAM
uint16_t *palette = nullptr;  // read every pixel -> slab

int16_t *dithOff = nullptr;  // frame() only -> PSRAM, page's 64 Bayer offsets
int16_t *kernelWork = nullptr; // slab: 16 scratch indices, 8 biases, 8 masks
const int16_t *sine = nullptr; // borrowed shared table, never released here

uint8_t lastP[4] = {255, 255, 255, 255};
uint32_t lastThemeGen = 0xFFFFFFFF;
int allocW = 0, allocH = 0, colStride = 0;
uint32_t contourPhase = 0;

// Table placement, against the 9,216 B per-animation slab:
//   colTermPh  7,680 B  read every pixel                   HOT
//   rowTerm      960 B  read once per row                  HOT
//   palette      512 B  read every pixel, data-dependent   HOT
//   kernelWork    64 B  indices and two PIE constants      HOT
//   -------------------------------------------------------------
//              9,216 B of 9,216 B at 480x480
//   colTerm      960 B  read only in frame()               PSRAM
//   themeRamp    512 B  read only when the palette is
//                       rebuilt                            PSRAM
//   dithOff      128 B  frame()'s column expansion          PSRAM
// Column phases have a stride rounded to eight int16 entries so every PIE
// span starts on 16 bytes even at 466/233 wide. At 480 this adds no padding.
// All allocations, including temporary kernel tables, belong to release().
void release();

bool init(int w, int h) {
    if (w <= 0 || h <= 0 || w > 480 || h > 480) {
        release(); // supported panels fit the fixed slab, larger ones do not
        return false;
    }
    if ((allocW != 0 && allocW != w) || (allocH != 0 && allocH != h)) {
        release();
    }
    sine = sinLut();
    if (sine == nullptr) {
        release();
        return false;
    }
    if (colTerm == nullptr) {
        colTerm = static_cast<int16_t *>(alloc(w * sizeof(int16_t)));
        if (colTerm == nullptr) {
            release(); // a partial set must not survive a failed init (gm-bzu.15)
            return false;
        }
        allocW = w;
        colStride = (w + 7) & ~7;
    }
    if (colTermPh == nullptr) {
        colTermPh = static_cast<int16_t *>(allocHot(8 * colStride * sizeof(int16_t)));
        if (colTermPh == nullptr) {
            release();
            return false;
        }
    }
    if (rowTerm == nullptr) {
        rowTerm = static_cast<int16_t *>(allocHot(h * sizeof(int16_t)));
        if (rowTerm == nullptr) {
            release();
            return false;
        }
        allocH = h;
    }
    if (themeRamp == nullptr) {
        themeRamp = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
        if (themeRamp == nullptr) {
            release();
            return false;
        }
    }
    if (palette == nullptr) {
        palette = static_cast<uint16_t *>(allocHot(256 * sizeof(uint16_t)));
        if (palette == nullptr) {
            release();
            return false;
        }
    }
    if (dithOff == nullptr) {
        dithOff = static_cast<int16_t *>(alloc(64 * sizeof(int16_t)));
        if (dithOff == nullptr) {
            release();
            return false;
        }
    }
    if (kernelWork == nullptr) {
        kernelWork = static_cast<int16_t *>(allocHot(32 * sizeof(int16_t)));
        if (kernelWork == nullptr) {
            release();
            return false;
        }
        for (int i = 0; i < 8; i++) {
            kernelWork[16 + i] = 128;
            kernelWork[24 + i] = 255;
        }
    }
    return true;
}

// A smooth odd shoulder, 1.5u - 0.5u^3 in Q8: -256 at u = -256, 0 at 0, 256 at
// 256, and flat at both ends so the bright regions do not keep climbing all
// the way to the panel edge.
int shoulderQ8(int u) {
    if (u > 256) {
        u = 256;
    }
    if (u < -256) {
        u = -256;
    }
    const int uu = (u * u) / 256; // u * u is non-negative, but keep the form uniform
    return (u * (768 - uu)) / 512;
}

// Shoulder (p[5]) moves the profile between a straight ramp and a steeper
// shoulder: 0 is the clamped ramp itself, 50 the shoulder exactly (the
// difference times 50 / 50 is the difference), 100 twice the shoulder's
// departure from the ramp, which saturates early and flattens a wide quiet
// plateau each side of the crossing. Clamped to the shoulder's own +/-256.
int profileQ8(int u, int knee) {
    const int uc = u > 256 ? 256 : (u < -256 ? -256 : u);
    int v = uc + (shoulderQ8(u) - uc) * knee / 50;
    return v > 256 ? 256 : (v < -256 ? -256 : v);
}

void buildPalette(int contrastP) {
    buildThemeRamp(themeRamp, 256);
    // The page wraps a cosine-shaped sample of the theme RAMP, not the theme
    // wheel. s is 0..512; s2 and s3 preserve that Q9 scale. These three powers
    // soften the shoulders and narrow the crest. Contrast sets peak=40..98;
    // the ramp positions span 10..(10+26+peak+44), at most 178.
    const int peak = 40 + contrastP * 58 / 100;
    for (int i = 0; i < 256; i++) {
        // 4 sine slots per palette entry and a 768-slot offset start at the
        // trough of one full cosine cycle, exactly as the page does.
        const int s = (sine[(i * 4 + 768) & (SIN_N - 1)] + 512) >> 1;
        const int s2 = (s * s) >> 9;
        const int s3 = (s2 * s) >> 9;
        const int v = 10 + ((s * 26) >> 9) + ((s2 * peak) >> 9) + ((s3 * 44) >> 9);
        palette[i] = themeRamp[v < 0 ? 0 : (v > 255 ? 255 : v)];
    }
    // The dither lives in column units, where eight units are one palette index
    // at the largest row value. Half the mean spacing, because the palette's
    // steep middle would otherwise show the pattern. Derive amplitude before
    // applying the contour phase, as the page does. lroundf matches its
    // lround helper, including negative halves; the shared sine table also
    // rounds to the same integer entries as the page's SIN table.
    const float amp = ditherAmp(palette, 256) * 0.5f;
    for (int k = 0; k < 64; k++) {
        const float d = (static_cast<float>(BAYER8[k]) - 31.5f) * (amp * 8.0f / 31.5f);
        dithOff[k] = static_cast<int16_t>(lroundf(d));
    }
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[BG_ANIM_PARAMS]) {
    if (memcmp(p, lastP, 4) != 0 || themeGen() != lastThemeGen) {
        memcpy(lastP, p, 4);
        lastThemeGen = themeGen();
        buildPalette(p[3]);
    }

    // Match JavaScript's >>>0 after each product, including long uptimes:
    // these are modulo-2^32 clocks, not floating seconds.
    // Speed follows the fleet's curve, speedMul(): 0.15x at 0, 1x at 50 and
    // 6.7x at 100 of the rate Speed 50 has since gm-33fm (bead gm-kh2s).
    // The multiplier is Q7 and 3712 at 50, the old rate of 29 with seven
    // fraction bits, and the four shifts below take those bits back, so
    // Speed 50 is the same picture: contours advance 29000/1024 = 28.320313
    // indices/s (9.039448 s per palette turn), the x and y drift periods are
    // 4.519724 s and 5.165399 s, and curvature breathes once per 3.013149 s.
    // The old affine law read 5 + p[0] * 48 / 100 and covered 0.17x to 1.83x,
    // the flattest slider in the fleet. Every phase below reads at most bit
    // 19 of its product, and Q7 keeps bits 0..24, so the uint32 wrap never
    // shows and the Speed 50 output is bit identical to the old law's.
    // Rounded, not truncated: the nearest .5 boundary over Speed 0..100 is
    // 11 float ulps away, so the host, the device and the page agree.
    const uint32_t speedQ7 = static_cast<uint32_t>(lroundf(3712.0f * speedMul(p[0])));
    const uint32_t base = tMs * speedQ7;
    // Contour flow (p[6]) scales only the contour slide, in Q4: 16 at 50 is
    // the old rate exactly, since (base * 16) >> 21 keeps bits 17..24 of base
    // as base >> 17 does; 0 holds the contours still while the centre drifts
    // and the curvature breathes, 32 at 100 slides them twice as fast. The
    // product wraps modulo 2^32, which only loses bits above the eight read.
    const uint32_t flowQ4 = static_cast<uint32_t>(p[6]) * 32u / 100u;
    contourPhase = ((base * flowQ4) >> 21) & 255;
    const uint32_t phD = base >> 14;
    const uint32_t phD2 = (base * 7u) >> 17;
    const uint32_t phK = (base * 3u) >> 15;
    const int16_t *sl = sine;
    for (int i = 0; i < 8; i++) {
        kernelWork[16 + i] = static_cast<int16_t>(128 + contourPhase);
    }

    // How far the crossings wander from the centre, as a fraction of the panel.
    const int driftW = w * static_cast<int>(p[2]) / 300; // up to one third of the width
    const int driftH = h * static_cast<int>(p[2]) / 300;
    // Divisions rather than shifts wherever the value can be negative: a
    // right shift of a negative operand is implementation-defined in C++17 and
    // none of this is per pixel, so there is nothing to buy by relying on it.
    const int x0 = w / 2 + (sl[phD & (SIN_N - 1)] * driftW) / 512;
    const int y0 = h / 2 + (sl[phD2 & (SIN_N - 1)] * driftH) / 512;

    // The shoulder half-width. A narrow crossing bends the hyperbolas tightly,
    // a wide one leaves a broad quiet band, which is what the slider is for.
    // Breathing (p[4]) sets the depth of that cycle: 34 at 50 gives
    // bend=222..290 in Q8, 1 +/- 34/256; 0 holds the curvature still and 100
    // swings it 1 +/- 68/256. Add a positive multiple of 512 before shifting
    // and subtract it afterwards: this reproduces the page's signed >>9 floor
    // without implementation-defined negative shifts (the sine is >= -512).
    const int breath = 34 * static_cast<int>(p[4]) / 50;
    const int bend = 256 + ((sl[phK & (SIN_N - 1)] * breath + breath * 512) >> 9) - breath;
    const int hwW = ((w / 4 + w * static_cast<int>(p[1]) / 200) * bend) >> 8;
    const int hwH = ((h / 4 + h * static_cast<int>(p[1]) / 200) * bend) >> 8;

    // Contour depth (p[7], default 100) scales the row factor and with it
    // how much of the palette the saddle spans: 127 at 100 is the old
    // ROW_MAX exactly, 36 at 0 shows about a quarter of the palette at once, a
    // broad glow that brightens and fades as the contours pass. It never
    // exceeds ROW_MAX, so the product stays in the range the bias and both
    // kernels are proven for (QEMU: every row value -127..127).
    const int depthP = p[7] > 100 ? 100 : p[7]; // the registry clamps too
    const int rowAmp = ROW_MAX * (40 + depthP) / 140;
    const int knee = p[5];
    for (int y = 0; y < h; y++) {
        const int u = ((y - y0) * 256) / (hwH > 0 ? hwH : 1);
        rowTerm[y] = static_cast<int16_t>(profileQ8(u, knee) * rowAmp / 256);
    }
    for (int x = 0; x < w; x++) {
        const int u = ((x - x0) * 256) / (hwW > 0 ? hwW : 1);
        colTerm[x] = static_cast<int16_t>(profileQ8(u, knee) * COL_MAX / 256);
    }
    for (int ph8 = 0; ph8 < 8; ph8++) {
        int16_t *dstPh = colTermPh + static_cast<size_t>(ph8) * colStride;
        const int16_t *off = &dithOff[ph8 * 8];
        for (int x = 0; x < w; x++) {
            int v = colTerm[x] + off[x & 7];
            v = v < -COL_MAX ? -COL_MAX : (v > COL_MAX ? COL_MAX : v);
            dstPh[x] = static_cast<int16_t>(v);
        }
    }
}

// Portable spec. |ct*rt| <= 1016*127 = 129032, so BIAS makes the shifted
// value nonnegative, in 1..254. The phase then wraps it through the cyclic
// palette. Absolute y selects both the row factor and the Bayer phase;
// single-row interlace calls and arbitrary band shapes see identical pixels.
GM_ANIM_IRAM void bandRef(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int y = y0; y < y0 + rows; y++) {
        const int rt = rowTerm[y];
        const int16_t *ct = colTermPh + static_cast<size_t>(y & 7) * colStride;
        int x = 0;
        for (; x + 1 < w; x += 2) {
            const uint16_t c0 = palette[(((ct[x] * rt + BIAS) >> 10) + contourPhase) & 255];
            const uint16_t c1 = palette[(((ct[x + 1] * rt + BIAS) >> 10) + contourPhase) & 255];
            *reinterpret_cast<uint32_t *>(dst) = static_cast<uint32_t>(c0) | (static_cast<uint32_t>(c1) << 16);
            dst += 2;
        }
        for (; x < w; x++) {
            *dst++ = palette[(((ct[x] * rt + BIAS) >> 10) + contourPhase) & 255];
        }
    }
}

#if GM_BGANIM_SADDLE_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
// First transcribe GCC 14.2's .L4 loop from bandRef (xtensa-asm14.sh,
// 2026-09-11): 21 instructions per pair, with independent work between
// every scalar load and its consumer. Keep this schedule for the short
// remainder after the PIE blocks. The zero-trip case is handled by LOOPNEZ.
// dst is 4-byte aligned; col needs only halfword alignment.
GM_ANIM_IRAM __attribute__((noinline)) void saddlePairsAsm(uint16_t *out, const int16_t *col,
                                                        const uint16_t *pal, int rt, int phase, int pairs) {
    int t0, t1;
    const int bias = 128 << 10;
    asm volatile("loopnez %[n], 1f\n"
                 "l16ui %[t1], %[col], 2\n"
                 "l16ui %[t0], %[col], 0\n"
                 "mul16s %[t1], %[t1], %[rt]\n"
                 "mul16s %[t0], %[t0], %[rt]\n"
                 "add %[t1], %[t1], %[bias]\n"
                 "srai %[t1], %[t1], 10\n"
                 "add %[t0], %[t0], %[bias]\n"
                 "add %[t1], %[t1], %[phase]\n"
                 "srai %[t0], %[t0], 10\n"
                 "extui %[t1], %[t1], 0, 8\n"
                 "add %[t0], %[t0], %[phase]\n"
                 "addx2 %[t1], %[t1], %[pal]\n"
                 "extui %[t0], %[t0], 0, 8\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "slli %[t1], %[t1], 16\n"
                 "or %[t1], %[t1], %[t0]\n"
                 "s32i %[t1], %[out], 0\n"
                 "addi %[col], %[col], 4\n"
                 "addi %[out], %[out], 4\n"
                 "1:\n"
                 : [out] "+&r"(out), [col] "+&r"(col), [t0] "=&r"(t0), [t1] "=&r"(t1)
                 : [pal] "r"(pal), [rt] "r"(rt), [bias] "r"(bias), [phase] "r"(phase), [n] "r"(pairs)
                 : "memory");
}

// PIE's signed multiply gives floor(ct*rt/1024), in -127..126. Adding
// 128+phase afterwards is exactly bandRef's positive-bias expression;
// the sum is 1..509, so VADDS.S16 cannot saturate. ANDQ with 255 implements
// the page's wrap. SAR=10 is set once per call. q0/q4 hold two groups of
// eight, q1 broadcasts the row factor, q2/q3 hold bias/mask. GCC never
// allocates q registers, so there is no q clobber syntax or need to list them.
// FreeRTOS owns coprocessor enabling and context save; never write CPENABLE.
//
// The edge over GCC is eight multiplies and shifts per VMUL, plus vector
// bias/wrap. Two independent groups hide the vector load and multiply
// interlocks documented by the KB's PIE pipeline table. Palette lookup is
// still a scalar gather: each pair loads both indices, then both colours,
// and packs one S32I with no adjacent load consumer. 83 instructions per
// 16 pixels (5.1875/pixel), versus GCC's 21/2 (10.5/pixel). The assembled
// body is 241 bytes, inside LOOPNEZ's 256-byte limit, with no modeled
// load-use gaps left. This is an issue-count estimate, not measured cycles:
// PIE throughput, scratch store forwarding and flash/cache costs still need
// the device A/B. Host time cannot decide whether this beats bandRef.
//
// col and work are 16-byte aligned by allocation and padded row stride;
// every load/store span stays in a complete 16-pixel block. rowFactor is
// naturally halfword aligned, all VLDBC.16 requires (QEMU primitive probe:
// all 65536 values, all eight halfword offsets). out only needs 4 bytes:
// PIE never stores directly to it. work[0..15] is scratch, [16..23] bias,
// [24..31] mask. No state survives the call; no band-neighbour dependency.
GM_ANIM_IRAM __attribute__((noinline)) void saddleBlocksAsm(uint16_t *out, const int16_t *col,
                                                         const int16_t *rowFactor, const uint16_t *pal,
                                                         int16_t *work, int blocks) {
    const int16_t *constants = work + 16;
    int t0, t1;
    asm volatile("ssai 10\n"
                 "ee.vldbc.16 q1, %[rt]\n"
                 "ee.vld.128.ip q2, %[constants], 16\n"
                 "ee.vld.128.ip q3, %[constants], 16\n"
                 "loopnez %[n], 1f\n"
                 "ee.vld.128.ip q0, %[col], 16\n"
                 "ee.vld.128.ip q4, %[col], 16\n"
                 "ee.vmul.s16 q0, q0, q1\n"
                 "ee.vmul.s16 q4, q4, q1\n"
                 "ee.vadds.s16 q0, q0, q2\n"
                 "ee.vadds.s16 q4, q4, q2\n"
                 "ee.andq q0, q0, q3\n"
                 "ee.andq q4, q4, q3\n"
                 "ee.vst.128.ip q0, %[work], 16\n"
                 "ee.vst.128.ip q4, %[work], -16\n"
                 "l16ui %[t0], %[work], 0\n"
                 "l16ui %[t1], %[work], 2\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "addx2 %[t1], %[t1], %[pal]\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "slli %[t1], %[t1], 16\n"
                 "or %[t1], %[t1], %[t0]\n"
                 "s32i %[t1], %[out], 0\n"
                 "l16ui %[t0], %[work], 4\n"
                 "l16ui %[t1], %[work], 6\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "addx2 %[t1], %[t1], %[pal]\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "slli %[t1], %[t1], 16\n"
                 "or %[t1], %[t1], %[t0]\n"
                 "s32i %[t1], %[out], 4\n"
                 "l16ui %[t0], %[work], 8\n"
                 "l16ui %[t1], %[work], 10\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "addx2 %[t1], %[t1], %[pal]\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "slli %[t1], %[t1], 16\n"
                 "or %[t1], %[t1], %[t0]\n"
                 "s32i %[t1], %[out], 8\n"
                 "l16ui %[t0], %[work], 12\n"
                 "l16ui %[t1], %[work], 14\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "addx2 %[t1], %[t1], %[pal]\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "slli %[t1], %[t1], 16\n"
                 "or %[t1], %[t1], %[t0]\n"
                 "s32i %[t1], %[out], 12\n"
                 "l16ui %[t0], %[work], 16\n"
                 "l16ui %[t1], %[work], 18\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "addx2 %[t1], %[t1], %[pal]\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "slli %[t1], %[t1], 16\n"
                 "or %[t1], %[t1], %[t0]\n"
                 "s32i %[t1], %[out], 16\n"
                 "l16ui %[t0], %[work], 20\n"
                 "l16ui %[t1], %[work], 22\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "addx2 %[t1], %[t1], %[pal]\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "slli %[t1], %[t1], 16\n"
                 "or %[t1], %[t1], %[t0]\n"
                 "s32i %[t1], %[out], 20\n"
                 "l16ui %[t0], %[work], 24\n"
                 "l16ui %[t1], %[work], 26\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "addx2 %[t1], %[t1], %[pal]\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "slli %[t1], %[t1], 16\n"
                 "or %[t1], %[t1], %[t0]\n"
                 "s32i %[t1], %[out], 24\n"
                 "l16ui %[t0], %[work], 28\n"
                 "l16ui %[t1], %[work], 30\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "addx2 %[t1], %[t1], %[pal]\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "slli %[t1], %[t1], 16\n"
                 "or %[t1], %[t1], %[t0]\n"
                 "s32i %[t1], %[out], 28\n"
                 "addi %[out], %[out], 32\n"
                 "1:\n"
                 : [out] "+&r"(out), [col] "+&r"(col), [work] "+&r"(work),
                   [constants] "+&r"(constants), [t0] "=&r"(t0), [t1] "=&r"(t1)
                 : [rt] "r"(rowFactor), [pal] "r"(pal), [n] "r"(blocks)
                 : "memory");
}
#endif

GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
#if GM_BGANIM_SADDLE_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
    // allocHot normally guarantees alignment. If it fell back to a heap
    // allocation while another animation occupied the slab, preserve the
    // contract without letting PIE round an unaligned pointer down.
    if (((reinterpret_cast<uintptr_t>(colTermPh) | reinterpret_cast<uintptr_t>(kernelWork)) & 15u) == 0) {
        for (int y = y0; y < y0 + rows; y++) {
            const int16_t *ct = colTermPh + static_cast<size_t>(y & 7) * colStride;
            const int blocks = w >> 4;
            if (blocks != 0) {
                saddleBlocksAsm(dst, ct, rowTerm + y, palette, kernelWork, blocks);
            }
            int x = blocks << 4;
            const int pairs = (w - x) >> 1;
            if (pairs != 0) {
                saddlePairsAsm(dst + x, ct + x, palette, rowTerm[y], contourPhase, pairs);
                x += pairs * 2;
            }
            if (x < w) {
                dst[x] = palette[(((ct[x] * rowTerm[y] + BIAS) >> 10) + contourPhase) & 255];
            }
            dst += w;
        }
        return;
    }
#endif
    bandRef(dst, y0, rows, w, tMs, p);
}

void release() {
    releaseTable(colTerm, static_cast<size_t>(allocW) * sizeof(int16_t));
    releaseTable(colTermPh, static_cast<size_t>(8 * colStride) * sizeof(int16_t));
    releaseTable(rowTerm, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(themeRamp, 256 * sizeof(uint16_t));
    releaseTable(palette, 256 * sizeof(uint16_t));
    releaseTable(dithOff, 64 * sizeof(int16_t));
    releaseTable(kernelWork, 32 * sizeof(int16_t));
    sine = nullptr;
    allocW = allocH = colStride = 0;
    contourPhase = 0;
    lastThemeGen = 0xFFFFFFFF;
    lastP[0] = lastP[1] = lastP[2] = lastP[3] = 255;
}

} // namespace

extern const BgAnimation bg_anim_saddle;
const BgAnimation bg_anim_saddle = {
    "saddle",
    "Saddle",
    {{"speed", "Speed", 50},
     {"curvature", "Curvature", 35},
     {"drift", "Drift", 25},
     {"contrast", "Contrast", 30},
     {"breath", "Breathing", 50},
     {"shoulder", "Shoulder", 50},
     {"flow", "Contour flow", 50},
     {"depth", "Contour depth", 100}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
