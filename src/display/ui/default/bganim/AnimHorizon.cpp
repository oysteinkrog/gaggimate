#ifndef GAGGIMATE_SIM

// "Horizon" - a curved, gently swelling horizon with a soft glow above it
// and a dim reflection below. This implements entry 15 of
// tools/animbench/web/anim_bench.html, including its unsigned time wraps.
// Two opposing sine waves ride a curvature parabola; two more move the whole
// vertical gradient. At Speed 50, the default, the swells travel about
// +23.81 and -13.64 px/s, and the main vertical drift repeats every 8.192 s.
//
// The field is separable in Q4 palette-index units:
//   pixel = palette[(colTermPh[(y & 7) * colStride + x] + rowTerm[y]) >> 4].
// The palette contains the sky/ground gradients and both quartic glows.
// Eight column copies carry the page's Bayer8 dither before the Q4 shift.
// Each row uses its absolute y, including single-row interlaced calls.
//
// All per-pixel work is integer. The page's truncating signed divisions are
// C++ divisions here, and its signed >> operations are arithmetic shifts on
// both supported compilers. Float builds the 64 dither offsets and the
// per frame speed multiplier, which is rounded to an integer before any
// time arithmetic.
// The host PPM writer expands RGB565 with channel*255/31 (or /63); the
// page replicates bits instead. Their RGB888 values can differ by one
// while the underlying panel RGB565 word agrees exactly.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <string.h>

// On by default as requested; device parity and a production A/B still
// decide the speed claim. Setting this to 0 renders bandRef on the device.
#ifndef GM_BGANIM_HORIZON_ASM
#define GM_BGANIM_HORIZON_ASM 1
#endif

namespace {
using namespace bganim;

constexpr int INDEX_MAX = 4095; // 12-bit Q4 sum, hence a 0..255 palette index
constexpr int ROW_SPAN = 2200;  // bottom-to-top rise in Q4 sum units
constexpr int CURVE_SPAN = 420; // maximum signed parabola amplitude
constexpr int HEIGHT_LO = 520, HEIGHT_HI = 1420; // Height slider's mean offset

int16_t *colCurve = nullptr;   // w samples, rebuilt in frame(): PSRAM
int16_t *colTermPh = nullptr;  // eight Bayer row phases, read per pixel: slab
int16_t *rowTerm = nullptr;    // one Q4 offset per absolute row: slab
uint16_t *themeRamp = nullptr; // 256 theme colours, palette rebuild only: PSRAM
uint16_t *palette = nullptr;   // 256 RGB565 colours, per-pixel gather: slab
int16_t *dithOff = nullptr;    // 64 Bayer offsets, frame() only: PSRAM

int allocW = 0, allocH = 0, colStride = 0;
uint8_t lastP[4] = {255, 255, 255, 255};
uint32_t lastThemeGen = 0xFFFFFFFF;

// At 480x480: colTermPh 7,680 B + rowTerm 960 B + palette 512 B =
// 9,152 B of the 9,216 B slab. colCurve 960 B, themeRamp 512 B and
// dithOff 128 B use PSRAM. colStride rounds w up to eight int16 samples,
// aligning every phase for PIE without changing the page's useful samples.
// At 466x466 the allocations plus slab alignment consume 9,008 B, and
// at 233x233 they consume 4,832 B. No pixel gather falls back to PSRAM.
void release();

bool init(int w, int h) {
    if (w != allocW || h != allocH) {
        release();
    }
    if (w <= 0 || h <= 0 || sinLut() == nullptr) {
        release();
        return false;
    }
    allocW = w;
    allocH = h;
    colStride = (w + 7) & ~7;
    if (colCurve == nullptr) {
        colCurve = static_cast<int16_t *>(alloc(static_cast<size_t>(w) * sizeof(int16_t)));
    }
    if (colTermPh == nullptr) {
        colTermPh = static_cast<int16_t *>(allocHot(static_cast<size_t>(8 * colStride) * sizeof(int16_t)));
    }
    if (rowTerm == nullptr) {
        rowTerm = static_cast<int16_t *>(allocHot(static_cast<size_t>(h) * sizeof(int16_t)));
    }
    if (themeRamp == nullptr) {
        themeRamp = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
    }
    if (palette == nullptr) {
        palette = static_cast<uint16_t *>(allocHot(256 * sizeof(uint16_t)));
    }
    if (dithOff == nullptr) {
        dithOff = static_cast<int16_t *>(alloc(64 * sizeof(int16_t)));
    }
    if (!colCurve || !colTermPh || !rowTerm || !themeRamp || !palette || !dithOff) {
        release(); // release the entire partial set before an OOM retry
        return false;
    }
    return true;
}

void buildHorizonPalette(int softP) {
    buildThemeRamp(themeRamp, 256);
    const int soft = 22 + softP * 50 / 100; // glow half-width: 22..72 indices
    const int glow = 100 + softP * 64 / 100; // main glow gain: 100..164
    for (int i = 0; i < 256; i++) {
        const int d = i - 128; // horizon at the middle of the palette
        // Actual page ramp positions: sky 42 down to 12, ground 10 up to
        // 34, before the two glows. Signed division truncates toward zero.
        int v = d >= 0 ? 42 - d * 30 / 127 : 34 + d * 24 / 128;
        int c = d - soft / 6; // main crest slightly above the horizon
        int ac = c < 0 ? -c : c;
        if (ac < soft) {
            const int k = 256 - ac * 256 / soft;
            const int kk = (k * k) >> 8; // Q8 triangle squared, then squared again
            v += (((kk * kk) >> 8) * glow) >> 8;
        }
        c = d + soft / 2; // reflection below, at one third the gain
        ac = c < 0 ? -c : c;
        if (d < 0 && ac < soft) {
            const int k = 256 - ac * 256 / soft;
            const int kk = (k * k) >> 8;
            v += (((kk * kk) >> 8) * (glow / 3)) >> 8;
        }
        palette[i] = themeRamp[v < 0 ? 0 : (v > 255 ? 255 : v)];
    }
    // bayerOffsets(ditherAmpJS(pal) * 0.45, 16): 16 converts palette
    // indices to Q4, and 31.5 centres the 0..63 Bayer matrix. lroundf
    // matches the page's lround (half away from zero).
    const float amp = ditherAmp(palette, 256) * 0.45f;
    for (int k = 0; k < 64; k++) {
        dithOff[k] = static_cast<int16_t>(lroundf((BAYER8[k] - 31.5f) * (amp * 16.0f / 31.5f)));
    }
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[BG_ANIM_PARAMS]) {
    const uint32_t gen = themeGen();
    if (memcmp(p, lastP, 4) != 0 || gen != lastThemeGen) {
        buildHorizonPalette(p[3]);
        lastThemeGen = gen;
        memcpy(lastP, p, 4);
    }

    // Exactly the page's >>> 0 after each multiplication, before shifting.
    // Speed follows the fleet's curve, speedMul(): 0.15x at 0, 1x at 50 and
    // 6.7x at 100 of the rate Speed 50 has since gm-33fm (bead gm-kh2s).
    // The multiplier is Q8 and 4096 at 50, the old sp of 16 with eight
    // fraction bits, and the four shifts take those bits back, so Speed 50
    // is the same picture: the main vertical phase advances 125 sine
    // samples/s (1024 per turn) and the other 78.125/s. The phases read
    // bits 15..27 of the product at most, so its wrap never shows. The old
    // affine law read 3 + p[0] * 26 / 100 and covered 0.19x to 1.8x.
    // Rounded, not truncated: the nearest .5 boundary over Speed 0..100 is
    // 38 float ulps away, so the host, the device and the page agree.
    const uint32_t speedQ8 = static_cast<uint32_t>(lroundf(4096.0f * speedMul(p[0])));
    const uint32_t base = tMs * speedQ8;
    const uint32_t phu = base >> 17, phu2 = (base * 3u) >> 18;
    const uint32_t phd = base >> 15, phd2 = (base * 5u) >> 18;
    const int16_t *sl = sinLut();
    const int cx = w / 2, span = cx > 0 ? cx : 1;
    const int k = (static_cast<int>(p[2]) - 50) * CURVE_SPAN / 50;
    int lo = 32767, hi = -32768;
    for (int x = 0; x < w; x++) {
        const int dx = x - cx;
        int q = dx * dx * 256 / (span * span); // squared radius, Q8
        if (q > 256) {
            q = 256;
        }
        // 21/16 and 55/16 sine samples/pixel. Subtract phu for rightward
        // travel, add phu2 for leftward travel. Amplitudes are Q4 units.
        const int v = ((k * q) >> 8) +
                      ((sl[(static_cast<uint32_t>((x * 21) >> 4) - phu) & (SIN_N - 1)] * 150) >> 9) +
                      ((sl[(static_cast<uint32_t>((x * 55) >> 4) + phu2) & (SIN_N - 1)] * 72) >> 9);
        colCurve[x] = static_cast<int16_t>(v);
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    for (int x = 0; x < w; x++) {
        colCurve[x] = static_cast<int16_t>(colCurve[x] - lo);
    }
    // Reserve the page's 40 sum units beyond the measured profile range,
    // then clamp dither to that range even when its amplitude is larger.
    const int colMax = hi - lo + 40;
    const int mean = HEIGHT_LO + static_cast<int>(p[1]) * (HEIGHT_HI - HEIGHT_LO) / 100;
    const int offset = mean + ((sl[phd & (SIN_N - 1)] * 240) >> 9) +
                       ((sl[phd2 & (SIN_N - 1)] * 110) >> 9);
    const int denom = h > 1 ? h - 1 : 1;
    const int rowHi = INDEX_MAX - colMax;
    for (int y = 0; y < h; y++) {
        int v = offset + (h - 1 - y) * ROW_SPAN / denom;
        rowTerm[y] = static_cast<int16_t>(v < 0 ? 0 : (v > rowHi ? rowHi : v));
    }
    for (int ph = 0; ph < 8; ph++) {
        int16_t *ct = colTermPh + static_cast<size_t>(ph) * colStride;
        for (int x = 0; x < w; x++) {
            const int v = colCurve[x] + dithOff[ph * 8 + (x & 7)];
            ct[x] = static_cast<int16_t>(v < 0 ? 0 : (v > colMax ? colMax : v));
        }
    }
}

// The page's sepBand, packing pairs into aligned 32-bit stores. Bounds are
// established in frame(): ct is 0..colMax and rt is 0..4095-colMax, hence
// sum is 0..4095 and the page's final &255 is redundant. No row state or
// dither phase depends on band size.
// Even at parameter extremes colMax <= 420 + 2*(150+72) + 40 = 904,
// so rowHi stays positive and PIE's signed 16-bit sum cannot saturate.
GM_ANIM_IRAM void bandRef(uint16_t *__restrict dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int y = y0; y < y0 + rows; y++) {
        const int rt = rowTerm[y];
        const int16_t *__restrict ct = colTermPh + static_cast<size_t>(y & 7) * colStride;
        const uint16_t *__restrict pal = palette;
        int x = 0;
        for (; x + 1 < w; x += 2) {
            const uint16_t c0 = pal[(ct[x] + rt) >> 4];
            const uint16_t c1 = pal[(ct[x + 1] + rt) >> 4];
            *reinterpret_cast<uint32_t *>(dst) = static_cast<uint32_t>(c0) | (static_cast<uint32_t>(c1) << 16);
            dst += 2;
        }
        if (x < w) {
            *dst++ = pal[(ct[x] + rt) >> 4];
        }
    }
}

#if GM_BGANIM_HORIZON_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
// GCC 14's bandRef .L4 loop was the starting schedule: l16si hi/lo,
// add hi, srai hi, add lo, addx2 hi, srai lo, l16ui hi, addx2 lo,
// l16ui lo, slli hi, or, s32i, then two pointer increments. That is
// 15 instructions/pair with every load-use gap filled already.
//
// The edge GCC cannot take is eight parallel Q4 adds in PIE. Keep the sums
// in q0, extract four 32-bit pairs with EE.MOVI.32.A, and use EXTUI at
// bits 4 and 20 for the two 8-bit indices. This avoids a vector store and
// eight scalar reloads through scratch. The palette remains a scalar
// gather, scheduled high then low so SLLI fills the low load's use gap.
// 43 instructions/eight pixels, 5.375 instructions/pixel, inside LOOPNEZ.
// The vector load's gap is the output pointer increment. With SRAM hits
// there are no unfilled load-use gaps in the body: 5.375 cycles/pixel is
// an issue-model floor, not a measured device time. Flash fetch, task
// preemption and per-row setup still need the production timing rung.
//
// ct is 16-byte aligned by allocHot and colStride, including 466/233
// widths. Only complete vectors are loaded, so padding is never read.
// out is only required to be 4-byte aligned, as BgAnim.h promises; PIE
// never stores through it. Subtracting 16 inside asm lets the loop's first
// increment fill the load gap without a negative-offset store. That
// temporary address is never dereferenced. No C++ pointer goes before out.
//
// q0/q1 are compiler-invisible PIE registers and need no GCC clobber names.
// Production never writes CPENABLE: FreeRTOS saves CP3 state lazily. This
// kernel does not change SAR. EE.MOVI.32.A selectors 0..3 were separately
// probed under QEMU before inclusion here; the full kernel has a verbatim
// twin in tools/qemubench/tests/anim_horizon/main.c.
GM_ANIM_IRAM __attribute__((noinline)) void horizonRowAsm(uint16_t *out, const int16_t *ct,
                                                        const uint16_t *pal, int rt, int w) {
    uint16_t *outp = out;
    const int16_t *ctp = ct;
    const uint32_t rowPair = (uint32_t)rt | ((uint32_t)rt << 16);
    const int groups = w >> 3;
    uint32_t packed, lo, hi;
    asm volatile("ee.movi.32.q q1, %[rt], 0\n"
                 "ee.movi.32.q q1, %[rt], 1\n"
                 "ee.movi.32.q q1, %[rt], 2\n"
                 "ee.movi.32.q q1, %[rt], 3\n"
                 "addi %[out], %[out], -16\n"
                 "loopnez %[n], 1f\n"
                 "ee.vld.128.ip q0, %[ct], 16\n"
                 "addi %[out], %[out], 16\n"
                 "ee.vadds.s16 q0, q0, q1\n"
                 "ee.movi.32.a q0, %[packed], 0\n"
                 "extui %[lo], %[packed], 4, 8\n"
                 "extui %[hi], %[packed], 20, 8\n"
                 "addx2 %[lo], %[lo], %[pal]\n"
                 "addx2 %[hi], %[hi], %[pal]\n"
                 "l16ui %[hi], %[hi], 0\n"
                 "l16ui %[lo], %[lo], 0\n"
                 "slli %[hi], %[hi], 16\n"
                 "or %[hi], %[hi], %[lo]\n"
                 "s32i %[hi], %[out], 0\n"
                 "ee.movi.32.a q0, %[packed], 1\n"
                 "extui %[lo], %[packed], 4, 8\n"
                 "extui %[hi], %[packed], 20, 8\n"
                 "addx2 %[lo], %[lo], %[pal]\n"
                 "addx2 %[hi], %[hi], %[pal]\n"
                 "l16ui %[hi], %[hi], 0\n"
                 "l16ui %[lo], %[lo], 0\n"
                 "slli %[hi], %[hi], 16\n"
                 "or %[hi], %[hi], %[lo]\n"
                 "s32i %[hi], %[out], 4\n"
                 "ee.movi.32.a q0, %[packed], 2\n"
                 "extui %[lo], %[packed], 4, 8\n"
                 "extui %[hi], %[packed], 20, 8\n"
                 "addx2 %[lo], %[lo], %[pal]\n"
                 "addx2 %[hi], %[hi], %[pal]\n"
                 "l16ui %[hi], %[hi], 0\n"
                 "l16ui %[lo], %[lo], 0\n"
                 "slli %[hi], %[hi], 16\n"
                 "or %[hi], %[hi], %[lo]\n"
                 "s32i %[hi], %[out], 8\n"
                 "ee.movi.32.a q0, %[packed], 3\n"
                 "extui %[lo], %[packed], 4, 8\n"
                 "extui %[hi], %[packed], 20, 8\n"
                 "addx2 %[lo], %[lo], %[pal]\n"
                 "addx2 %[hi], %[hi], %[pal]\n"
                 "l16ui %[hi], %[hi], 0\n"
                 "l16ui %[lo], %[lo], 0\n"
                 "slli %[hi], %[hi], 16\n"
                 "or %[hi], %[hi], %[lo]\n"
                 "s32i %[hi], %[out], 12\n"
                 "1:\n"
                 : [out] "+&r"(outp), [ct] "+&r"(ctp), [packed] "=&r"(packed), [lo] "=&r"(lo), [hi] "=&r"(hi)
                 : [rt] "r"(rowPair), [n] "r"(groups), [pal] "r"(pal)
                 : "memory");
    // At most seven remaining pixels, including the 233-wide odd tail.
    // Use the original pointers: the asm output cursor names its last group.
    for (int x = groups << 3; x < w; x++) {
        out[x] = pal[(ct[x] + rt) >> 4];
    }
}

GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int y = y0; y < y0 + rows; y++) {
        horizonRowAsm(dst, colTermPh + static_cast<size_t>(y & 7) * colStride, palette, rowTerm[y], w);
        dst += w;
    }
}
#else
GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
    bandRef(dst, y0, rows, w, tMs, p);
}
#endif

void release() {
    releaseTable(colCurve, static_cast<size_t>(allocW) * sizeof(int16_t));
    releaseTable(colTermPh, static_cast<size_t>(8 * colStride) * sizeof(int16_t));
    releaseTable(rowTerm, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(themeRamp, 256 * sizeof(uint16_t));
    releaseTable(palette, 256 * sizeof(uint16_t));
    releaseTable(dithOff, 64 * sizeof(int16_t));
    allocW = allocH = colStride = 0;
    lastThemeGen = 0xFFFFFFFF;
    lastP[0] = lastP[1] = lastP[2] = lastP[3] = 255;
}

} // namespace

extern const BgAnimation bg_anim_horizon;
const BgAnimation bg_anim_horizon = {
    "horizon",
    "Horizon",
    {{"speed", "Speed", 50}, {"height", "Height", 45}, {"curvature", "Curvature", 35}, {"softness", "Softness", 60}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
