#ifndef GAGGIMATE_SIM

// "Caustics" — three plane waves at slowly rotating angles sum into drifting
// light filaments (threshold + square) on deep blue-black water. Per pixel:
// three phase-accumulator LUT sins + a palette fetch. Design: anim-water
// (Fable), 2026-08-15.

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

uint8_t *palR = nullptr, *palG = nullptr, *palB = nullptr; // theme-baked, 256 each
uint32_t lastThemeGen = 0xFFFFFFFF;

float g_cosA[K], g_sinA[K], g_freq[K], g_phase[K], g_step[K];
float g_thresh = 0.5f, g_invSpan = 2.0f;

void buildThemePalette() {
    for (int i = 0; i < 256; i++) {
        uint8_t c[3];
        themeRGB(i, c);
        palR[i] = c[0];
        palG[i] = c[1];
        palB[i] = c[2];
    }
}

bool init(int, int) {
    if (palR == nullptr) {
        palR = static_cast<uint8_t *>(alloc(256));
        palG = static_cast<uint8_t *>(alloc(256));
        palB = static_cast<uint8_t *>(alloc(256));
    }
    if (palR == nullptr || palG == nullptr || palB == nullptr) {
        return false;
    }
    buildThemePalette();
    lastThemeGen = themeGen();
    return true;
}

void frame(uint32_t tMs, int, int, const uint8_t p[4]) {
    if (themeGen() != lastThemeGen) {
        buildThemePalette();
        lastThemeGen = themeGen();
    }
    const float t = tMs * 0.001f;
    const float freqScale = lerpf(0.55f, 1.9f, p[1] / 100.0f);
    const float speedScale = 0.8f * speedMul(p[0]);
    g_thresh = 0.14f + 0.55f * (p[2] / 100.0f);
    g_invSpan = 1.0f / fmaxf(1e-3f, 1.0f - g_thresh);
    for (int k = 0; k < K; k++) {
        const float ang = BASE_ANGLE[k] + ANG_DRIFT[k] * t;
        g_cosA[k] = fastCosRad(ang);
        g_sinA[k] = fastSinRad(ang);
        g_freq[k] = FREQ_BASE[k] * freqScale;
        g_phase[k] = PHASE0[k] + t * speedScale * SPEED_MUL[k] * 2.0f;
        g_step[k] = g_cosA[k] * g_freq[k];
    }
}

void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int yy = 0; yy < rows; yy++) {
        const int y = y0 + yy;
        float proj[K];
        for (int k = 0; k < K; k++) {
            proj[k] = y * g_sinA[k] * g_freq[k] + g_phase[k];
        }
        uint16_t *row = dst + static_cast<size_t>(yy) * w;
        for (int x = 0; x < w; x++) {
            float sum = 0;
            for (int k = 0; k < K; k++) {
                sum += fastSinRad(proj[k]);
                proj[k] += g_step[k];
            }
            float bright = (fabsf(sum * (1.0f / K)) - g_thresh) * g_invSpan;
            bright = bright < 0 ? 0 : (bright > 1 ? 1 : bright);
            bright *= bright;
            const int li = static_cast<int>(bright * 255.0f);
            const float dith = (BAYER4[(y & 3) * 4 + (x & 3)] - 7.5f) * 0.5f;
            row[x] = rgb565(clamp8f(palR[li] + dith), clamp8f(palG[li] + dith), clamp8f(palB[li] + dith));
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
