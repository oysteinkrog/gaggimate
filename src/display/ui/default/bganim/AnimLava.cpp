#ifndef GAGGIMATE_SIM

// "Lava" — six soft metaballs on incommensurate orbits, cubic falloff with a
// t^6 hot core, palette-mapped. Design: anim-fluid (Fable), 2026-08-15.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>
#include <string.h>

namespace {
using namespace bganim;

constexpr int NUM_BLOBS = 6;

struct BlobDef {
    float fx1, fx2, fy1, fy2, ax1, ax2, ay1, ay2, px1, px2, py1, py2, cx, cy, R0, Rpulse, wR, phR;
};
struct BlobState {
    float bx, by, R2, invR2;
};

BlobDef blobDef[NUM_BLOBS];
BlobState blob[NUM_BLOBS];
uint16_t *paletteLUT = nullptr;
float *fieldRow = nullptr; // one row of accumulated field
uint32_t lastThemeGen = 0xFFFFFFFF;
bool inited = false;

bool init(int w, int h) {
    if (paletteLUT == nullptr) {
        paletteLUT = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
    }
    if (fieldRow == nullptr) {
        fieldRow = static_cast<float *>(alloc(w * sizeof(float)));
    }
    if (paletteLUT == nullptr || fieldRow == nullptr) {
        return false;
    }
    if (!inited) {
        inited = true;
        for (int i = 0; i < NUM_BLOBS; i++) {
            const float ga = i * 2.39996323f; // golden angle spreads phases
            BlobDef &d = blobDef[i];
            d.fx1 = 0.55f + 0.11f * i;
            d.fx2 = 1.41421356f * (0.35f + 0.05f * i);
            d.fy1 = 0.63f + 0.09f * ((i * 3) % 5);
            d.fy2 = 1.73205081f * (0.30f + 0.04f * i);
            d.ax1 = w * 0.14f;
            d.ax2 = w * 0.07f;
            d.ay1 = h * 0.14f;
            d.ay2 = h * 0.07f;
            d.px1 = ga;
            d.px2 = ga * 2.1f;
            d.py1 = ga * 1.7f;
            d.py2 = ga * 0.6f;
            d.cx = w * 0.5f + w * 0.28f * cosf(ga);
            d.cy = h * 0.5f + h * 0.28f * sinf(ga * 1.3f);
            const float m = (w < h ? w : h);
            d.R0 = m * 0.19f;
            d.Rpulse = m * 0.05f;
            d.wR = 0.00011f + 0.00003f * i;
            d.phR = ga * 2.7f;
        }
        buildThemeRamp(paletteLUT, 256);
        lastThemeGen = themeGen();
    }
    return true;
}

float g_intensity = 1.0f;

void frame(uint32_t tMs, int, int, const uint8_t p[4]) {
    const float omega0 = 6.2831853f / 45000.0f * speedMul(p[0]); // 45s base cycle at speed 50
    const float sizeMul = 0.6f + (p[1] / 100.0f);
    g_intensity = 0.5f + (p[2] / 100.0f) * 1.3f;
    if (themeGen() != lastThemeGen) {
        buildThemeRamp(paletteLUT, 256);
        lastThemeGen = themeGen();
    }
    const float t = tMs * omega0;
    for (int i = 0; i < NUM_BLOBS; i++) {
        const BlobDef &d = blobDef[i];
        blob[i].bx = d.cx + d.ax1 * fastSinRad(t * d.fx1 + d.px1) + d.ax2 * fastSinRad(t * d.fx2 * 1.7f + d.px2);
        blob[i].by = d.cy + d.ay1 * fastCosRad(t * d.fy1 * 1.13f + d.py1) + d.ay2 * fastSinRad(t * d.fy2 * 0.9f + d.py2);
        const float R = (d.R0 + d.Rpulse * fastSinRad(tMs * d.wR + d.phR)) * sizeMul;
        blob[i].R2 = R * R;
        blob[i].invR2 = 1.0f / blob[i].R2;
    }
}

void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const float intensity = g_intensity;
    for (int row = 0; row < rows; row++) {
        const int y = y0 + row;
        memset(fieldRow, 0, w * sizeof(float));
        for (int i = 0; i < NUM_BLOBS; i++) {
            const float dy = y - blob[i].by;
            const float dy2 = dy * dy;
            if (dy2 >= blob[i].R2) {
                continue;
            }
            const float half = sqrtf(blob[i].R2 - dy2); // one sqrt per blob per touched row
            int xlo = static_cast<int>(blob[i].bx - half);
            if (xlo < 0) {
                xlo = 0;
            }
            int xhi = static_cast<int>(blob[i].bx + half);
            if (xhi > w - 1) {
                xhi = w - 1;
            }
            const float invR2 = blob[i].invR2;
            const float bx = blob[i].bx;
            for (int x = xlo; x <= xhi; x++) {
                const float dx = x - bx;
                const float d2 = dx * dx + dy2;
                const float tt = 1.0f - d2 * invR2;
                if (tt <= 0) {
                    continue;
                }
                const float t3 = tt * tt * tt;
                fieldRow[x] += t3 * intensity + (tt > 0.7f ? t3 * t3 * intensity * 0.6f : 0.0f);
            }
        }
        uint16_t *out = dst + static_cast<size_t>(row) * w;
        for (int x = 0; x < w; x++) {
            float f = fieldRow[x];
            if (f > 1.6f) {
                f = 1.6f;
            }
            const float dith = (BAYER4[(y & 3) * 4 + (x & 3)] / 16.0f - 0.5f) * (1.0f / 128.0f);
            int idx = static_cast<int>((f / 1.6f + dith) * 255.0f);
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

extern const BgAnimation bg_anim_lava;
const BgAnimation bg_anim_lava = {
    "lava",
    "Lava",
    {{"speed", "Speed", 50}, {"scale", "Blob size", 50}, {"glow", "Glow", 60}, {nullptr, nullptr, 0}},
    init,
    frame,
    band,
};

#endif // GAGGIMATE_SIM
