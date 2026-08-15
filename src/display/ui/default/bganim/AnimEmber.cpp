#ifndef GAGGIMATE_SIM

// "Ember" — a warm glow breathing from below screen center, like coals in a
// hearth. Three incommensurate breathing periods (11.3s/17.7s/6.1s) so the
// pattern never visibly repeats; optional edge-of-perception flicker from the
// shared tileable noise texture. Radial field is an incremental r^2 walk (two
// adds per pixel) into a radius LUT — no sqrt in the loop.
// Design: anim-atmosphere (Fable), 2026-08-15.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

namespace {
using namespace bganim;

// DDS phase steps: full circle = 2^32, periods 11.3s / 17.7s / 6.1s.
constexpr uint32_t STEP1 = static_cast<uint32_t>(4294967296.0 / 11300.0);
constexpr uint32_t STEP2 = static_cast<uint32_t>(4294967296.0 / 17700.0);
constexpr uint32_t STEP3 = static_cast<uint32_t>(4294967296.0 / 6100.0);
constexpr uint32_t PHOFF2 = static_cast<uint32_t>(1.7 / 6.2831853 * 4294967296.0);
constexpr uint32_t PHOFF3 = static_cast<uint32_t>(4.2 / 6.2831853 * 4294967296.0);
constexpr int RSHIFT = 9; // r^2 -> radiusLUT bucket

uint16_t *palette = nullptr;  // 256 entries, reversed theme ramp (bright core)
uint8_t *radiusLUT = nullptr; // r^2>>RSHIFT -> normalized radius byte
const uint8_t *noise = nullptr;
uint32_t lastThemeGen = 0xFFFFFFFF;
uint8_t lastGlow = 255;
int g_cx = 240, g_cy = 260;
float g_maxR = 353.7f;
int g_breathe = 0, g_flickerAmp = 0, g_sx = 0, g_sy = 0;

void buildRadiusLut(uint8_t glow) {
    const float glowGain = 0.55f + 0.014f * glow;
    const float scale = 255.0f / (g_maxR * glowGain);
    for (int i = 0; i < 256; i++) {
        const float r = sqrtf(static_cast<float>(i << RSHIFT));
        radiusLUT[i] = clamp8f(r * scale);
    }
}

bool init(int w, int h) {
    if (palette == nullptr) {
        palette = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
        radiusLUT = static_cast<uint8_t *>(alloc(256));
        noise = noiseTex256();
        if (palette == nullptr || radiusLUT == nullptr || noise == nullptr) {
            return false;
        }
    }
    g_cx = w / 2;
    g_cy = h / 2 + (20 * h) / 480;
    const float dx = static_cast<float>(g_cx);
    const float dy = static_cast<float>(g_cy > h - g_cy ? g_cy : h - g_cy);
    g_maxR = sqrtf(dx * dx + dy * dy);
    lastThemeGen = 0xFFFFFFFF;
    lastGlow = 255;
    return true;
}

void frame(uint32_t tMs, int, int, const uint8_t p[4]) {
    if (themeGen() != lastThemeGen) {
        buildThemeRamp(palette, 256, /*reversed=*/true); // brightest stop at the core
        lastThemeGen = themeGen();
    }
    if (p[1] != lastGlow) {
        buildRadiusLut(p[1]);
        lastGlow = p[1];
    }
    const float spd = speedMul(p[0]);
    // Speed scales virtual time; a param change causes one phase jump, which
    // the slow breathing envelope absorbs invisibly.
    const uint32_t vt = static_cast<uint32_t>(tMs * spd);
    const float pulseGain = p[3] / 100.0f;
    const float s1 = sin1024((vt * STEP1) >> 22) * (1.0f / SIN_AMP);
    const float s2 = sin1024(((vt * STEP2) + PHOFF2) >> 22) * (1.0f / SIN_AMP);
    const float s3 = sin1024(((vt * STEP3) + PHOFF3) >> 22) * (1.0f / SIN_AMP);
    g_breathe = static_cast<int>(pulseGain * (0.30f * s1 + 0.15f * s2 + 0.05f * s3) * 70.0f);
    g_flickerAmp = (10 * p[2]) / 100;
    g_sx = static_cast<int>((vt * 6u) >> 10) & 255;
    g_sy = static_cast<int>((vt * 4u) >> 10) & 255;
}

void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int r = 0; r < rows; r++) {
        const int y = y0 + r;
        const int dy = y - g_cy;
        const int dy2 = dy * dy;
        uint16_t *row = dst + static_cast<size_t>(r) * w;
        const uint8_t *bayerRow = &BAYER8[(y & 7) * 8];
        int8_t dith[8];
        for (int k = 0; k < 8; k++) {
            dith[k] = static_cast<int8_t>((static_cast<int>(bayerRow[k]) - 31) / 5);
        }
        const uint8_t *noiseRow = g_flickerAmp ? noise + ((y + g_sy) & 255) * 256 : nullptr;
        int dx = -g_cx;
        int r2 = dx * dx + dy2;
        for (int x = 0; x < w; x++) {
            int ridx = r2 >> RSHIFT;
            if (ridx > 255) {
                ridx = 255;
            }
            int rn = radiusLUT[ridx] - g_breathe + dith[x & 7];
            if (noiseRow != nullptr) {
                rn += ((static_cast<int>(noiseRow[(x + g_sx) & 255]) - 128) * g_flickerAmp) >> 7;
            }
            if (rn < 0) {
                rn = 0;
            } else if (rn > 255) {
                rn = 255;
            }
            row[x] = palette[rn];
            r2 += (dx << 1) + 1;
            dx++;
        }
    }
}

} // namespace

extern const BgAnimation bg_anim_ember;
const BgAnimation bg_anim_ember = {
    "ember",
    "Ember",
    {{"speed", "Speed", 50}, {"glow", "Glow size", 45}, {"flicker", "Flicker", 20}, {"pulse", "Pulse", 50}},
    init,
    frame,
    band,
};

#endif // GAGGIMATE_SIM
