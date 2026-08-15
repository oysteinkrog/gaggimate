#ifndef GAGGIMATE_SIM

// "Aurora" — two domain-warped sine curtains over a dark sky. The warp terms
// depend only on y and t (per-row constants); per pixel is two DDS phase
// accumulators + LUT reads, then a palette lookup and saturating add over the
// row-constant sky color. Design: anim-celestial (Fable), 2026-08-15.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

namespace {
using namespace bganim;

uint16_t *glowLUT = nullptr; // [256 intensity] -> RGB565 glow color (hue-baked)
int lastHue = -1;

// f1 = 0.026, f1b = 0.017 rad/px -> phase steps in Q8 ticks of the 1024-LUT.
constexpr float TICKS = 1024.0f * 256.0f / 6.2831853f; // rad -> Q8 LUT ticks
constexpr uint32_t STEP1 = static_cast<uint32_t>(0.026f * TICKS);
constexpr uint32_t STEP2 = static_cast<uint32_t>(0.017f * TICKS);

float g_t = 0, g_A1 = 0, g_A2 = 0;
int32_t g_inten14 = 0; // intensity * 1.4 in Q8

void lerp3(const float a[3], const float b[3], float m, float out[3]) {
    for (int i = 0; i < 3; i++) {
        out[i] = a[i] + (b[i] - a[i]) * m;
    }
}

void buildGlowLUT(uint8_t hueP) {
    const float hueShift = hueP / 100.0f;
    const float green[3] = {10, 140, 90}, teal[3] = {15, 170, 150}, violet[3] = {90, 60, 180}, white[3] = {200, 255, 230};
    float base[3];
    if (hueShift < 0.5f) {
        lerp3(green, teal, hueShift * 2.0f, base);
    } else {
        lerp3(teal, violet, (hueShift - 0.5f) * 2.0f * 0.6f, base);
    }
    for (int i = 0; i < 256; i++) {
        const float inten = i / 255.0f;
        float core[3];
        lerp3(base, white, fminf(1.0f, inten * 1.3f), core);
        const float scale = fminf(1.0f, inten * 2.2f);
        glowLUT[i] = rgb565(clamp8f(core[0] * scale), clamp8f(core[1] * scale), clamp8f(core[2] * scale));
    }
}

bool init(int, int) {
    if (sinLut() == nullptr) {
        return false;
    }
    if (glowLUT == nullptr) {
        glowLUT = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
    }
    if (glowLUT == nullptr) {
        return false;
    }
    if (lastHue < 0) {
        buildGlowLUT(35);
        lastHue = 35;
    }
    return true;
}

void frame(uint32_t tMs, int, int, const uint8_t p[4]) {
    if (p[2] != lastHue) {
        buildGlowLUT(p[2]);
        lastHue = p[2];
    }
    g_t = (tMs * 0.001f) * (0.15f + (p[1] / 100.0f) * 0.6f);
    g_A1 = 0.6f + (p[3] / 100.0f) * 2.4f;
    g_A2 = 0.4f + (p[3] / 100.0f) * 1.6f;
    g_inten14 = static_cast<int32_t>((p[0] / 100.0f) * 1.4f * 256.0f);
}

inline int16_t sinTick(uint32_t tickQ8) { return sinLut()[(tickQ8 >> 8) & 1023]; }

void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const float t = g_t;
    for (int ry = 0; ry < rows; ry++) {
        const int y = y0 + ry;
        const float warp1 = fastSinRad(y * 0.021f + t * 0.5f) * g_A1;
        const float warp2 = fastSinRad(y * 0.013f - t * 0.44f + 1.7f) * g_A2;
        const float yn = y / 480.0f;
        float env = 1.0f - fabsf(yn - 0.32f) / 0.85f;
        env = env < 0 ? 0 : env * env;
        const int32_t envQ12 = static_cast<int32_t>(env * 4096.0f);
        const uint8_t bgR = static_cast<uint8_t>(2 + yn * 1);
        const uint8_t bgG = static_cast<uint8_t>(3 + yn * 2);
        const uint8_t bgB = static_cast<uint8_t>(10 + (1 - yn) * 4);
        const int bgR5 = bgR >> 3, bgG6 = bgG >> 2, bgB5 = bgB >> 3;

        // Row phase starts (rad -> Q8 ticks); negatives wrap via uint32.
        uint32_t ph1 = static_cast<uint32_t>(static_cast<int64_t>((warp1 + t * 0.12f) * TICKS));
        uint32_t ph2 = static_cast<uint32_t>(static_cast<int64_t>((warp2 + t * 0.07f) * TICKS));

        uint16_t *row = dst + static_cast<size_t>(ry) * w;
        for (int x = 0; x < w; x++) {
            const int32_t v1 = sinTick(ph1); // -512..512
            const int32_t v2 = sinTick(ph2);
            ph1 += STEP1;
            ph2 += STEP2;
            int32_t v = (v1 * 635 + v2 * 393) >> 7; // 0.62/0.38 weights -> ~±4096
            if (v < 0) {
                v = 0;
            }
            v = (v * v) >> 12; // square -> 0..4096
            int32_t inten = ((v * envQ12) >> 12) * g_inten14 >> 12;
            inten += (BAYER8[(y & 7) * 8 + (x & 7)] - 32) >> 2; // ordered dither
            if (inten < 0) {
                inten = 0;
            } else if (inten > 255) {
                inten = 255;
            }
            const uint16_t glow = glowLUT[inten];
            int r = bgR5 + ((glow >> 11) & 0x1F);
            int g = bgG6 + ((glow >> 5) & 0x3F);
            int b = bgB5 + (glow & 0x1F);
            if (r > 0x1F) {
                r = 0x1F;
            }
            if (g > 0x3F) {
                g = 0x3F;
            }
            if (b > 0x1F) {
                b = 0x1F;
            }
            row[x] = static_cast<uint16_t>((r << 11) | (g << 5) | b);
        }
    }
}

} // namespace

extern const BgAnimation bg_anim_aurora;
const BgAnimation bg_anim_aurora = {
    "aurora",
    "Aurora",
    {{"intensity", "Intensity", 55}, {"speed", "Speed", 40}, {"hue", "Hue shift", 35}, {"waviness", "Waviness", 50}},
    init,
    frame,
    band,
};

#endif // GAGGIMATE_SIM
