#ifndef GAGGIMATE_SIM

// "Caustics" — three plane waves at slowly rotating angles sum into drifting
// light filaments (threshold + square) on deep blue-black water.
//
// Optimized (opt-caustics, 2026-08-15): the original evaluated three float
// LUT sines per pixel plus float threshold/square/palette shaping — the
// worst band() cost in the fleet. Techniques applied:
//
// 1. Coarse-grid sampling. The interference field (sum of three waves) is
//    smooth relative to a few pixels, so each wave's phase is only evaluated
//    exactly every GRID=4 columns; the raw integer sum is linearly
//    interpolated across the span (one subtract+shift per span, one integer
//    add per pixel — no per-pixel trig or division). The nonlinear
//    threshold/square shaping still runs per exact pixel (via shapeLUT
//    below), so only the smooth wave-sum itself is approximated.
// 2. Fixed-point DDS phase accumulators (uint32_t, full circle = 2^32,
//    matching the scheme AnimEmber already uses) index bganim::sin1024's
//    backing LUT directly via `>> 22` — zero float per pixel, zero libm
//    anywhere in band().
// 3. bganim::sinLut() itself is NOT inlined (lazy-init guard in a different
//    translation unit) — the xtensa-asm tool showed every sin1024() call
//    compiling to a real device CALL, which also disqualifies the loop from
//    GCC's zero-overhead LOOP instruction. Fixed by fetching the LUT pointer
//    once per band() call and indexing it directly thereafter.
// 4. Nonlinear shaping (abs, threshold, invSpan, square, *255) depends only
//    on the frame-constant thresh/invSpan and the pixel's wave-sum magnitude
//    (0..3*512), so it's baked into a per-frame shapeLUT — one lookup
//    replaces several float ops. The table is over-sized past the true max
//    so interpolation overshoot needs no clamp branch.
// 5. Dither-baked RGB565 palette buckets. There are only 16 distinct dither
//    values (4x4 Bayer), so instead of adding dither to each of R/G/B and
//    clamping/packing per pixel, 16 full 256-entry RGB565 tables (dither
//    already added, clamped, and packed) are rebuilt once per frame. Per
//    pixel this turns "3 adds + 3 clamps + rgb565 pack" into a single
//    uint16_t lookup indexed by the shading index.
// 6. Branchless abs (shift/xor/sub) removes the remaining per-pixel branch.
// Design: anim-water (Fable), 2026-08-15.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

namespace {
using namespace bganim;

constexpr int K = 3;
constexpr float BASE_ANGLE[K] = {0.35f, 2.55f, 4.55f};
constexpr float ANG_DRIFT[K] = {0.014f, -0.010f, 0.007f};
constexpr float PHASE0[K] = {0.0f, 2.1f, 4.6f};
constexpr float FREQ_BASE[K] = {0.046f, 0.061f, 0.037f};
constexpr float SPEED_MUL[K] = {1.0f, 0.82f, 1.28f};

// DDS phase scale: a uint32_t phase accumulator's full range (2^32) maps to
// one full circle (2*pi radians), same convention as AnimEmber's STEP1/2/3.
// idx = phaseQ >> 22 lands in [0,1023], directly indexing sin1024's LUT
// (SIN_N == 1024 == 2^(32-22)).
constexpr double PHASE_SCALE = 4294967296.0 / 6.283185307179586;
constexpr int PHASE_SHIFT = 22; // 32 - log2(SIN_N)

// Coarse-grid stride in x: the wave-sum field is evaluated exactly every
// GRID columns and linearly interpolated in between (see file header). Must
// be a power of two (interpolation step uses a shift, not a divide).
constexpr int GRID = 4;

// |sum of 3 sin1024 outputs| ranges 0..K*SIN_AMP inclusive; shapeLUT maps
// that magnitude straight to a shading index (0..255), threshold+square
// baked in, rebuilt once per frame from thresh/invSpan. Sized with margin
// past SHAPE_MAX so integer-interpolation overshoot (bounded by GRID-1)
// never needs a clamp branch in band(); the formula naturally saturates
// there since normalized bright already clamps to 1.
constexpr int SHAPE_MAX = K * SIN_AMP; // 1536
constexpr int SHAPE_PAD = 16;          // > GRID-1 worst-case overshoot
constexpr int SHAPE_N = SHAPE_MAX + SHAPE_PAD;

// Only 16 distinct dither offsets exist (4x4 Bayer); rgbLUT holds a full
// dither-applied, clamped, RGB565-packed 256-entry palette per offset, so
// band() never adds/clamps/packs per pixel — just one lookup.
constexpr int DITHER_N = 16;

uint16_t *rgbLUT = nullptr; // [DITHER_N][256], flattened
uint32_t lastThemeGen = 0xFFFFFFFF;

uint32_t g_rowFreqQ[K]; // per-row-unit phase step (y * this), DDS units
uint32_t g_phaseQ[K];   // phase intercept, DDS units
uint32_t g_stepQ[K];    // per-pixel (x) phase step, DDS units
int8_t ditherI[DITHER_N]; // (BAYER4[i]-7.5)*0.5, precomputed once at init
uint8_t shapeLUT[SHAPE_N];

void buildThemePalette() {
    uint8_t baseR[256], baseG[256], baseB[256];
    for (int i = 0; i < 256; i++) {
        uint8_t c[3];
        themeRGB(i, c);
        baseR[i] = c[0];
        baseG[i] = c[1];
        baseB[i] = c[2];
    }
    for (int di = 0; di < DITHER_N; di++) {
        const int d = ditherI[di];
        uint16_t *bucket = rgbLUT + di * 256;
        for (int li = 0; li < 256; li++) {
            int r = baseR[li] + d;
            int g = baseG[li] + d;
            int b = baseB[li] + d;
            r = r < 0 ? 0 : (r > 255 ? 255 : r);
            g = g < 0 ? 0 : (g > 255 ? 255 : g);
            b = b < 0 ? 0 : (b > 255 ? 255 : b);
            bucket[li] = rgb565(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b));
        }
    }
}

bool init(int, int) {
    if (rgbLUT == nullptr) {
        rgbLUT = static_cast<uint16_t *>(alloc(DITHER_N * 256 * sizeof(uint16_t)));
    }
    if (rgbLUT == nullptr) {
        return false;
    }
    for (int i = 0; i < DITHER_N; i++) {
        ditherI[i] = static_cast<int8_t>(lroundf((BAYER4[i] - 7.5f) * 0.5f));
    }
    buildThemePalette();
    lastThemeGen = themeGen();
    return true;
}

// The DDS phase words are deliberately modular — only the low 32 bits are
// ever used (band() masks with SIN_N-1 after shifting). But the values being
// converted leave the range of uint32_t almost immediately: the phase
// intercept grows without bound with uptime, and sinA*freq / cosA*freq are
// negative for most of the angle sweep. Converting a double that is negative
// or >= 2^32 directly to uint32_t is undefined behaviour, not a wrap: the
// platform may saturate instead. x86 happens to wrap, which is exactly why
// the host harness matched the reference frames while the real target's
// behaviour was never actually guaranteed — and why -fsanitize=undefined
// alone stayed silent here (GCC does not fold float-cast-overflow into it).
// Reduce into int64_t first, where the conversion is defined, and let the
// integer-to-unsigned conversion perform the modular wrap the design wants.
inline uint32_t ddsQ(double v) { return static_cast<uint32_t>(static_cast<int64_t>(fmod(v, 4294967296.0))); }

void frame(uint32_t tMs, int, int, const uint8_t p[4]) {
    if (themeGen() != lastThemeGen) {
        buildThemePalette();
        lastThemeGen = themeGen();
    }
    const float t = tMs * 0.001f;
    const float freqScale = lerpf(0.55f, 1.9f, p[1] / 100.0f);
    const float speedScale = 0.8f * speedMul(p[0]);
    const float thresh = 0.14f + 0.55f * (p[2] / 100.0f); // p[2] = "contrast" param
    const float invSpan = 1.0f / fmaxf(1e-3f, 1.0f - thresh);

    for (int k = 0; k < K; k++) {
        const float ang = BASE_ANGLE[k] + ANG_DRIFT[k] * t;
        const float cosA = fastCosRad(ang);
        const float sinA = fastSinRad(ang);
        const float freq = FREQ_BASE[k] * freqScale; // rad/pixel
        const float phaseRad = PHASE0[k] + t * speedScale * SPEED_MUL[k] * 2.0f;
        g_rowFreqQ[k] = ddsQ(static_cast<double>(sinA * freq) * PHASE_SCALE);
        g_phaseQ[k] = ddsQ(static_cast<double>(phaseRad) * PHASE_SCALE);
        g_stepQ[k] = ddsQ(static_cast<double>(cosA * freq) * PHASE_SCALE);
    }

    // Nonlinear shaping LUT: |sum of 3 sin1024 outputs| -> shading index
    // (0..255). Rebuilt once per frame — libm/float here is free per the
    // brief (this is frame(), not the per-pixel band() loop). Entries past
    // SHAPE_MAX are still computed with the same formula; the bright clamp
    // (i*NORM can exceed 1) makes them saturate to the same value
    // shapeLUT[SHAPE_MAX] would hold, so band() never needs to clamp mag.
    constexpr float NORM = 1.0f / static_cast<float>(SHAPE_MAX);
    for (int i = 0; i < SHAPE_N; i++) {
        float bright = (i * NORM - thresh) * invSpan;
        bright = bright < 0 ? 0 : (bright > 1 ? 1 : bright);
        bright *= bright;
        shapeLUT[i] = static_cast<uint8_t>(bright * 255.0f + 0.5f);
    }
}

void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    // Fetch the shared sine LUT pointer once: bganim::sin1024() is inline
    // but its backing bganim::sinLut() is a plain (non-inline) function
    // with a lazy-init guard in another translation unit, so every call
    // compiles to a real device CALL — which also blocks GCC's
    // zero-overhead LOOP instruction. Cache the pointer and index it
    // directly instead of calling sin1024() in the loops below.
    const int16_t *lut = sinLut();

    for (int yy = 0; yy < rows; yy++) {
        const int y = y0 + yy;
        uint32_t phaseQ[K];
        for (int k = 0; k < K; k++) {
            phaseQ[k] = static_cast<uint32_t>(y) * g_rowFreqQ[k] + g_phaseQ[k];
        }
        uint16_t *row = dst + static_cast<size_t>(yy) * w;
        // This row's 4 dither buckets (x&3): the same 4 repeat for every
        // GRID-aligned span, so resolve the bucket pointers once per row,
        // not per pixel — band()'s inner loop is then a single lookup.
        const int dyBase = (y & 3) << 2;
        const uint16_t *rgb0 = rgbLUT + (dyBase | 0) * 256;
        const uint16_t *rgb1 = rgbLUT + (dyBase | 1) * 256;
        const uint16_t *rgb2 = rgbLUT + (dyBase | 2) * 256;
        const uint16_t *rgb3 = rgbLUT + (dyBase | 3) * 256;

        int x = 0;
        int32_t sumCur = lut[(phaseQ[0] >> PHASE_SHIFT) & (SIN_N - 1)] + lut[(phaseQ[1] >> PHASE_SHIFT) & (SIN_N - 1)] +
                          lut[(phaseQ[2] >> PHASE_SHIFT) & (SIN_N - 1)];

        // Main loop: GRID is a compile-time constant here (the tail below
        // handles any remainder), so the step multiply becomes a shift and
        // the 4-wide inner body is fully unrolled — no per-span branch, no
        // loop-trip-count check inside the hot path.
        while (x + GRID <= w) {
            const uint32_t np0 = phaseQ[0] + (g_stepQ[0] << 2);
            const uint32_t np1 = phaseQ[1] + (g_stepQ[1] << 2);
            const uint32_t np2 = phaseQ[2] + (g_stepQ[2] << 2);
            const int32_t sumNext = lut[(np0 >> PHASE_SHIFT) & (SIN_N - 1)] + lut[(np1 >> PHASE_SHIFT) & (SIN_N - 1)] +
                                     lut[(np2 >> PHASE_SHIFT) & (SIN_N - 1)];
            const int32_t stepInterp = (sumNext - sumCur) >> 2; // GRID==4

            int32_t val = sumCur;
            int32_t m = val >> 31;
            row[x + 0] = rgb0[shapeLUT[(val ^ m) - m]];
            val += stepInterp;
            m = val >> 31;
            row[x + 1] = rgb1[shapeLUT[(val ^ m) - m]];
            val += stepInterp;
            m = val >> 31;
            row[x + 2] = rgb2[shapeLUT[(val ^ m) - m]];
            val += stepInterp;
            m = val >> 31;
            row[x + 3] = rgb3[shapeLUT[(val ^ m) - m]];

            phaseQ[0] = np0;
            phaseQ[1] = np1;
            phaseQ[2] = np2;
            sumCur = sumNext;
            x += GRID;
        }

        // Tail shorter than GRID (only when w % GRID != 0): exact per-pixel
        // evaluation, no interpolation. 480 % 4 == 0 on the real panel, so
        // this path is untaken there.
        for (int i = 0; x < w; i++, x++) {
            const int32_t sum = lut[((phaseQ[0] + g_stepQ[0] * static_cast<uint32_t>(i)) >> PHASE_SHIFT) & (SIN_N - 1)] +
                                 lut[((phaseQ[1] + g_stepQ[1] * static_cast<uint32_t>(i)) >> PHASE_SHIFT) & (SIN_N - 1)] +
                                 lut[((phaseQ[2] + g_stepQ[2] * static_cast<uint32_t>(i)) >> PHASE_SHIFT) & (SIN_N - 1)];
            const int32_t m = sum >> 31;
            const uint16_t *rgbRow = rgbLUT + (dyBase | (x & 3)) * 256;
            row[x] = rgbRow[shapeLUT[(sum ^ m) - m]];
        }
    }
}

} // namespace

extern const BgAnimation bg_anim_caustics;
const BgAnimation bg_anim_caustics = {
    "caustics",
    "Caustics",
    {{"speed", "Drift speed", 50}, {"scale", "Cell scale", 45}, {"contrast", "Contrast", 55}, {nullptr, nullptr, 0}},
    init,
    frame,
    band,
};

#endif // GAGGIMATE_SIM
