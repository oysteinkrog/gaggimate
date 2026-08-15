#ifndef GAGGIMATE_SIM

// "Silk" — three slowly rotating plane waves interfere into a moiré sheen,
// contrast-curved and vignetted. Phase kept as a wrapping uint32 turn
// accumulator (DDS style): per pixel = 3 LUT reads + 3 adds.
//
// Row and temporal phase are ALSO plain Q32 turn accumulators now (same
// trick AnimPlasma.cpp uses for its "phase"/"cycle" fields): a uint32_t
// wraps mod 2^32 for free on every add/multiply, and one turn == 2^32, so
// wraparound IS the mod-2*pi reduction. That replaces the previous
// per-row `fmodf(ky*y + wt, 2*pi)` (1440 fmodf/frame, the whole cost of
// this animation) with a per-row integer add and a per-band-call integer
// multiply — zero libm in band(). The only float->int casts left are of
// small, frame-scoped magnitudes (kx/ky/wRate * TURN, all comfortably
// inside int32 range — see report for the bound), never of an unbounded
// growing phase, so there's no float->uint32 UB risk.
// Design: anim-fluid (Fable), 2026-08-15. Optimized: anim-fluid, 2026-08-15.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

namespace {
using namespace bganim;

struct SilkWave {
    float A0, rotMult, wMult, wk, phk, kx, ky;
};
SilkWave wave[3] = {
    {0.20f, 0.6f, 1.00f, 6.2831853f / 71000.0f, 0.4f, 0, 0},
    {2.15f, -1.0f, 1.37f, 6.2831853f / 95000.0f, 2.1f, 0, 0},
    {4.35f, 1.4f, 0.71f, 6.2831853f / 123000.0f, 4.0f, 0, 0},
};

uint16_t *paletteLUT = nullptr;
// contrastLUT is indexed DIRECTLY by (s + 1536), s being the raw 3-wave sine
// sum (range -1536..1536, 3073 values) — no more scaling s down to a 0..255
// index first. That skips both the "*255/3072" reduction (which the
// compiler was already turning into a 64-bit magic-number multiply, several
// instructions in the hot loop) and the int->float conversion of the old
// uint8_t table, since the entries are stored pre-converted to float.
constexpr int CONTRAST_N = 3073; // 2*1536 + 1
float *contrastLUT = nullptr;
// dx2LUT[x] = (x - cx)^2, hoisted out of the row loop (cx is row-invariant,
// so recomputing it 480 times/frame in the old code was pure waste).
float *dx2LUT = nullptr;
// Dither LUT folds the BAYER4 "/16, -0.5 center, *255/160 rescale" chain
// into a single lookup so band() spends one array read instead of two
// subtracts and two multiplies per pixel.
float ditherLUT[16];
uint32_t lastThemeGen = 0xFFFFFFFF;
int lastGlow = -1;
float g_invR2 = 1.0f;
float g_vignK = 0.32f; // 0.32f * g_invR2, folded so band() does one multiply instead of two
int32_t g_step[3];    // per-pixel x-phase step, Q32 turns/px
int32_t g_rowStep[3]; // per-row y-phase step, Q32 turns/row
uint32_t g_wtTurn[3]; // temporal phase at y=0, Q32 turns (already mod 2*pi via wraparound)
// sinLut() lives in BgAnimCommon.cpp (a different translation unit — this
// build has no LTO), so calling it from the pixel loop is a real, un-inlined
// function call with a lazy-init branch, 3x/pixel = 691200 calls/frame. That
// call overhead was the actual dominant cost of this animation, not the
// fmodf (removing fmodf alone barely moved host time). Cache the pointer
// once at init and index it directly in band() instead.
const int16_t *g_sinLut = nullptr;

// idx spans 0..CONTRAST_N-1 (== s+1536, s being the raw sine sum): this is
// the same gamma curve as before (powf(n255/255,e)*255 where n255 was s
// rescaled to 0..255) but applied directly at 3073-point resolution instead
// of quantizing to 256 levels first, so it's at least as accurate, not less.
void buildContrastLUT(uint8_t glow) {
    const float e = 0.6f + 2.0f * (glow / 100.0f);
    for (int idx = 0; idx < CONTRAST_N; idx++) {
        contrastLUT[idx] = powf(idx / static_cast<float>(CONTRAST_N - 1), e) * 255.0f;
    }
}

bool init(int w, int h) {
    g_sinLut = sinLut();
    if (g_sinLut == nullptr) {
        return false;
    }
    if (paletteLUT == nullptr) {
        paletteLUT = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
    }
    if (contrastLUT == nullptr) {
        contrastLUT = static_cast<float *>(alloc(CONTRAST_N * sizeof(float)));
    }
    if (paletteLUT == nullptr || contrastLUT == nullptr) {
        return false;
    }
    const float R = (w < h ? w : h) * 0.5f;
    g_invR2 = 1.0f / (R * R);
    g_vignK = 0.32f * g_invR2;
    if (dx2LUT == nullptr) {
        dx2LUT = static_cast<float *>(alloc(w * sizeof(float)));
        if (dx2LUT != nullptr) {
            // Pre-scaled by g_vignK (constant for the panel's lifetime) so
            // band()'s per-pixel vignette work is a single subtract —
            // env = envRowBase - dx2LUT[x] — instead of a load + add(dy2) +
            // multiply(vignK) + subtract every pixel.
            const float cx = w * 0.5f;
            for (int x = 0; x < w; x++) {
                const float dx = x - cx;
                dx2LUT[x] = g_vignK * dx * dx;
            }
        }
    }
    if (dx2LUT == nullptr) {
        return false;
    }
    if (lastGlow < 0) {
        buildThemeRamp(paletteLUT, 256);
        lastThemeGen = themeGen();
        buildContrastLUT(55);
        lastGlow = 55;
        for (int k = 0; k < 16; k++) {
            ditherLUT[k] = (BAYER4[k] / 16.0f - 0.5f) * (255.0f / 160.0f);
        }
    }
    return true;
}

// Phase accumulator: full uint32 range = one turn; the shared 1024-entry sine
// LUT is indexed by the top 10 bits. Indexes g_sinLut directly (see comment
// above) rather than calling sinLut() per pixel.
inline int16_t sinFromTurn(uint32_t turn) { return g_sinLut[turn >> 22]; }
constexpr float TURN = 4294967296.0f / 6.2831853f;

void frame(uint32_t tMs, int, int, const uint8_t p[4]) {
    const float omega0 = 6.2831853f / 55000.0f * speedMul(p[0]); // 55s base drift at speed 50
    const float k0 = 0.008f + 0.022f * (p[1] / 100.0f);
    if (themeGen() != lastThemeGen) {
        buildThemeRamp(paletteLUT, 256);
        lastThemeGen = themeGen();
    }
    if (p[2] != lastGlow) {
        buildContrastLUT(p[2]);
        lastGlow = p[2];
    }
    for (int i = 0; i < 3; i++) {
        SilkWave &wv = wave[i];
        const float A = wv.A0 + (omega0 * 0.15f * wv.rotMult) * tMs;
        const float k = k0 * (1.0f + 0.15f * fastSinRad(wv.wk * tMs + wv.phk));
        wv.kx = k * fastCosRad(A);
        wv.ky = k * fastSinRad(A);
        g_step[i] = static_cast<int32_t>(wv.kx * TURN);
        g_rowStep[i] = static_cast<int32_t>(wv.ky * TURN);
        // Temporal phase as a 64-bit Q32 product: wRateQ (turns/ms) times
        // tMs (ms) wraps mod 2^32 exactly like mod-one-turn, so the result
        // is already phase-mod-2*pi with no fmodf and no risk of the old
        // float->uint32 overflow (wRate*tMs as a float would blow way past
        // uint32 range once tMs runs into the minutes/hours).
        const float wRate = omega0 * wv.wMult; // rad/ms
        const int32_t wRateQ = static_cast<int32_t>(wRate * TURN); // turns/ms, Q32
        g_wtTurn[i] = static_cast<uint32_t>(static_cast<int64_t>(wRateQ) * static_cast<int64_t>(tMs));
    }
}

void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const float cy = w * 0.5f;
    // Seed each wave's row-phase at y0, then step by g_rowStep per row
    // (plain uint32 add — wraps mod 2*pi for free, replacing the old
    // per-row fmodf).
    uint32_t base[3];
    for (int i = 0; i < 3; i++) {
        base[i] = g_wtTurn[i] + static_cast<uint32_t>(g_rowStep[i]) * static_cast<uint32_t>(y0);
    }
    for (int row = 0; row < rows; row++) {
        const int y = y0 + row;
        const float dy = y - cy;
        // dx2LUT[x] is pre-scaled by g_vignK, so folding the row's dy
        // contribution in here too collapses the per-pixel vignette math
        // down to a single subtract (see dx2LUT comment in init()).
        const float envRowBase = 1.0f - g_vignK * dy * dy;
        uint16_t *out = dst + static_cast<size_t>(row) * w;
        uint32_t a = base[0], b = base[1], c = base[2];
        for (int x = 0; x < w; x++) {
            const int32_t s = sinFromTurn(a) + sinFromTurn(b) + sinFromTurn(c); // -1536..1536
            a += g_step[0];
            b += g_step[1];
            c += g_step[2];
            const float nc = contrastLUT[s + 1536]; // direct index, no scale-down + reconvert
            float env = envRowBase - dx2LUT[x];
            if (env < 0) {
                env = 0;
            }
            // nc*env is already in 0..255 units, and ditherLUT is pre-scaled
            // to match — no /255-then-*255 round trip like the old code.
            float idxf = nc * env + ditherLUT[(y & 3) * 4 + (x & 3)];
            int idx = static_cast<int>(idxf);
            if (idx < 0) {
                idx = 0;
            } else if (idx > 255) {
                idx = 255;
            }
            out[x] = paletteLUT[idx];
        }
        base[0] += static_cast<uint32_t>(g_rowStep[0]);
        base[1] += static_cast<uint32_t>(g_rowStep[1]);
        base[2] += static_cast<uint32_t>(g_rowStep[2]);
    }
}

} // namespace

extern const BgAnimation bg_anim_silk;
const BgAnimation bg_anim_silk = {
    "silk",
    "Silk",
    {{"speed", "Speed", 50}, {"scale", "Fringe density", 45}, {"glow", "Sheen", 55}, {nullptr, nullptr, 0}},
    init,
    frame,
    band,
};

#endif // GAGGIMATE_SIM
