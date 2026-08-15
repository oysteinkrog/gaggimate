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

struct Stop {
    float t, r, g, b;
};
constexpr Stop BLUE_STOPS[4] = {{0.0f, 2, 4, 10}, {0.55f, 14, 40, 64}, {0.85f, 90, 182, 226}, {1.0f, 206, 236, 250}};
constexpr Stop TEAL_STOPS[4] = {{0.0f, 2, 5, 9}, {0.55f, 12, 52, 46}, {0.85f, 66, 200, 164}, {1.0f, 216, 252, 236}};

uint8_t *palR = nullptr, *palG = nullptr, *palB = nullptr; // hue-blended, 256 each
int lastHue = -1;

float g_cosA[K], g_sinA[K], g_freq[K], g_phase[K], g_step[K];
float g_thresh = 0.5f, g_invSpan = 2.0f;

void gradientAt(const Stop *stops, float t, float out[3]) {
    int s = 0;
    while (s < 2 && t > stops[s + 1].t) {
        s++;
    }
    float span = stops[s + 1].t - stops[s].t;
    if (span < 1e-6f) {
        span = 1e-6f;
    }
    float tt = (t - stops[s].t) / span;
    tt = tt < 0 ? 0 : (tt > 1 ? 1 : tt);
    out[0] = stops[s].r + (stops[s + 1].r - stops[s].r) * tt;
    out[1] = stops[s].g + (stops[s + 1].g - stops[s].g) * tt;
    out[2] = stops[s].b + (stops[s + 1].b - stops[s].b) * tt;
}

void buildBlendedPalette(uint8_t hueP) {
    const float hueT = hueP / 100.0f;
    for (int i = 0; i < 256; i++) {
        const float t = i / 255.0f;
        float cb[3], ct[3];
        gradientAt(BLUE_STOPS, t, cb);
        gradientAt(TEAL_STOPS, t, ct);
        palR[i] = static_cast<uint8_t>(cb[0] + (ct[0] - cb[0]) * hueT);
        palG[i] = static_cast<uint8_t>(cb[1] + (ct[1] - cb[1]) * hueT);
        palB[i] = static_cast<uint8_t>(cb[2] + (ct[2] - cb[2]) * hueT);
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
    if (lastHue < 0) {
        buildBlendedPalette(50);
        lastHue = 50;
    }
    return true;
}

void frame(uint32_t tMs, int, int, const uint8_t p[4]) {
    if (p[3] != lastHue) {
        buildBlendedPalette(p[3]);
        lastHue = p[3];
    }
    const float t = tMs * 0.001f;
    const float freqScale = lerpf(0.55f, 1.9f, p[1] / 100.0f);
    const float speedScale = lerpf(0.15f, 1.6f, p[0] / 100.0f);
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
    {{"speed", "Drift speed", 35}, {"scale", "Cell scale", 45}, {"contrast", "Contrast", 55}, {"hue", "Hue", 50}},
    init,
    frame,
    band,
};

#endif // GAGGIMATE_SIM
