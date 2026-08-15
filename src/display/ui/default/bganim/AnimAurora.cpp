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

uint16_t *glowLUT = nullptr; // [256 intensity] -> RGB565 glow color (theme-baked)
uint32_t lastThemeGen = 0xFFFFFFFF;

// f1 = 0.026, f1b = 0.017 rad/px -> phase steps in Q8 ticks of the 1024-LUT.
constexpr float TICKS = 1024.0f * 256.0f / 6.2831853f; // rad -> Q8 LUT ticks
constexpr uint32_t STEP1 = static_cast<uint32_t>(0.026f * TICKS);
constexpr uint32_t STEP2 = static_cast<uint32_t>(0.017f * TICKS);

float g_t = 0, g_A1 = 0, g_A2 = 0;
int32_t g_inten14 = 0; // intensity * 1.4 in Q8

// Curtain color rides the theme's mid-to-bright range; the fade ramp keeps
// low intensities near-black so the additive blend stays subtle.
void buildGlowLUT() {
    for (int i = 0; i < 256; i++) {
        uint8_t c[3];
        themeRGB(40 + ((i * 215) >> 8), c);
        const float scale = fminf(1.0f, (i / 255.0f) * 2.2f);
        glowLUT[i] = rgb565(clamp8f(c[0] * scale), clamp8f(c[1] * scale), clamp8f(c[2] * scale));
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
    buildGlowLUT();
    lastThemeGen = themeGen();
    return true;
}

void frame(uint32_t tMs, int, int, const uint8_t p[4]) {
    if (themeGen() != lastThemeGen) {
        buildGlowLUT();
        lastThemeGen = themeGen();
    }
    g_t = (tMs * 0.001f) * 0.45f * speedMul(p[0]);
    g_A1 = 0.6f + (p[2] / 100.0f) * 2.4f;
    g_A2 = 0.4f + (p[2] / 100.0f) * 1.6f;
    g_inten14 = static_cast<int32_t>((p[1] / 100.0f) * 1.4f * 256.0f);
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
        uint8_t bg[3];
        themeRGB(static_cast<int>(yn * 10.0f), bg); // sky sits in the darkest ~4% of the theme
        const int bgR5 = bg[0] >> 3, bgG6 = bg[1] >> 2, bgB5 = bg[2] >> 3;

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
    {{"speed", "Speed", 50}, {"intensity", "Intensity", 55}, {"waviness", "Waviness", 50}, {nullptr, nullptr, 0}},
    init,
    frame,
    band,
};

#endif // GAGGIMATE_SIM
