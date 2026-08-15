#ifndef GAGGIMATE_SIM

// "Silk" — three slowly rotating plane waves interfere into a moiré sheen,
// contrast-curved and vignetted. Phase kept as a wrapping uint32 turn
// accumulator (DDS style): per pixel = 3 LUT reads + 3 adds.
// Design: anim-fluid (Fable), 2026-08-15.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

namespace {
using namespace bganim;

struct SilkWave {
    float A0, rotMult, wMult, wk, phk, kx, ky, wt;
};
SilkWave wave[3] = {
    {0.20f, 0.6f, 1.00f, 6.2831853f / 71000.0f, 0.4f, 0, 0, 0},
    {2.15f, -1.0f, 1.37f, 6.2831853f / 95000.0f, 2.1f, 0, 0, 0},
    {4.35f, 1.4f, 0.71f, 6.2831853f / 123000.0f, 4.0f, 0, 0, 0},
};

uint16_t *paletteLUT = nullptr;
uint8_t *contrastLUT = nullptr;
uint32_t lastThemeGen = 0xFFFFFFFF;
int lastGlow = -1;
float g_invR2 = 1.0f;
int32_t g_step[3];

void buildContrastLUT(uint8_t glow) {
    const float e = 0.6f + 2.0f * (glow / 100.0f);
    for (int i = 0; i < 256; i++) {
        contrastLUT[i] = static_cast<uint8_t>(powf(i / 255.0f, e) * 255.0f + 0.5f);
    }
}

bool init(int w, int h) {
    if (sinLut() == nullptr) {
        return false;
    }
    if (paletteLUT == nullptr) {
        paletteLUT = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
    }
    if (contrastLUT == nullptr) {
        contrastLUT = static_cast<uint8_t *>(alloc(256));
    }
    if (paletteLUT == nullptr || contrastLUT == nullptr) {
        return false;
    }
    const float R = (w < h ? w : h) * 0.5f;
    g_invR2 = 1.0f / (R * R);
    if (lastGlow < 0) {
        buildThemeRamp(paletteLUT, 256);
        lastThemeGen = themeGen();
        buildContrastLUT(55);
        lastGlow = 55;
    }
    return true;
}

// Phase accumulator: full uint32 range = one turn; the shared 1024-entry sine
// LUT is indexed by the top 10 bits.
inline int16_t sinFromTurn(uint32_t turn) { return sinLut()[turn >> 22]; }
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
        wv.wt = (omega0 * wv.wMult) * tMs;
        g_step[i] = static_cast<int32_t>(wv.kx * TURN);
    }
}

void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const float cx = w * 0.5f, cy = w * 0.5f;
    for (int row = 0; row < rows; row++) {
        const int y = y0 + row;
        const float dy = y - cy;
        const float dy2 = dy * dy;
        uint32_t base[3];
        for (int i = 0; i < 3; i++) {
            float wrap = fmodf(wave[i].ky * y + wave[i].wt, 6.2831853f);
            if (wrap < 0) {
                wrap += 6.2831853f; // float->uint32 of a negative is UB on Xtensa
            }
            base[i] = static_cast<uint32_t>(wrap * TURN);
        }
        uint16_t *out = dst + static_cast<size_t>(row) * w;
        uint32_t a = base[0], b = base[1], c = base[2];
        for (int x = 0; x < w; x++) {
            const int32_t s = sinFromTurn(a) + sinFromTurn(b) + sinFromTurn(c); // -1536..1536
            a += g_step[0];
            b += g_step[1];
            c += g_step[2];
            int n255 = ((s + 1536) * 255) / 3072;
            const uint8_t nc = contrastLUT[n255];
            const float dx = x - cx;
            float env = 1.0f - 0.32f * (dx * dx + dy2) * g_invR2;
            if (env < 0) {
                env = 0;
            }
            const float v = (nc * (1.0f / 255.0f)) * env;
            const float dith = (BAYER4[(y & 3) * 4 + (x & 3)] / 16.0f - 0.5f) * (1.0f / 160.0f);
            int idx = static_cast<int>((v + dith) * 255.0f);
            if (idx < 0) {
                idx = 0;
            } else if (idx > 255) {
                idx = 255;
            }
            out[x] = paletteLUT[idx];
        }
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
