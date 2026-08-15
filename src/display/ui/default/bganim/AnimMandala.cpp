#ifndef GAGGIMATE_SIM

// "Mandala" — N-fold rotational symmetry built from angular harmonics
// (sin(N*theta)), which are smooth and periodic by construction — no fold
// seams. Angle via fast atan2 (reciprocal LUT + minimax poly), radius via
// bucketed sqrt LUT. Design: anim-geometric (Fable), 2026-08-15.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

namespace {
using namespace bganim;

int16_t *sin256 = nullptr;   // Q7 sine, 256 entries
int16_t *sqrtLUT = nullptr;  // r2 bucket -> radius (bucket 192)
uint32_t *recipLUT = nullptr; // Q16 65536/(i+1), 0..240
uint8_t *vigLUT = nullptr;   // radius 0..255 -> falloff Q8
uint16_t *paletteLUT = nullptr;
uint32_t lastThemeGen = 0xFFFFFFFF;
uint16_t g_outside = 0;

void buildThemePalette() {
    buildThemeRamp(paletteLUT, 256);
    uint8_t c[3];
    themeRGB(0, c);
    g_outside = rgb565(c[0], c[1], c[2]);
}

bool init(int, int) {
    if (sin256 == nullptr) {
        sin256 = static_cast<int16_t *>(alloc(256 * sizeof(int16_t)));
        sqrtLUT = static_cast<int16_t *>(alloc(602 * sizeof(int16_t)));
        recipLUT = static_cast<uint32_t *>(alloc(241 * sizeof(uint32_t)));
        vigLUT = static_cast<uint8_t *>(alloc(256));
        paletteLUT = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
        if (sin256 == nullptr || sqrtLUT == nullptr || recipLUT == nullptr || vigLUT == nullptr || paletteLUT == nullptr) {
            return false;
        }
        for (int i = 0; i < 256; i++) {
            sin256[i] = static_cast<int16_t>(lroundf(127.0f * sinf(i * 6.2831853f / 256.0f)));
        }
        for (int i = 0; i < 602; i++) {
            sqrtLUT[i] = static_cast<int16_t>(lroundf(sqrtf(i * 192.0f)));
        }
        for (int i = 0; i < 241; i++) {
            recipLUT[i] = static_cast<uint32_t>(lroundf(65536.0f / (i + 1)));
        }
        for (int i = 0; i < 256; i++) {
            vigLUT[i] = static_cast<uint8_t>(lroundf(255.0f * powf(1.0f - i / 255.0f, 0.55f)));
        }
        buildThemePalette();
        lastThemeGen = themeGen();
    }
    return true;
}

int g_N = 8, g_tOffA = 0, g_tOffB = 0, g_rOffsetScale = 0;
int g_breatheQ8 = 256;

void frame(uint32_t tMs, int, int, const uint8_t p[4]) {
    if (themeGen() != lastThemeGen) {
        buildThemePalette();
        lastThemeGen = themeGen();
    }
    g_N = 4 + (p[1] * 8) / 100;
    const float t = tMs * 0.001f * 0.35f * speedMul(p[0]);
    const float turb = 0.25f + (p[2] / 100.0f) * 1.1f;
    g_rOffsetScale = static_cast<int>(turb * 18.0f);
    // 40.74 = 256 ticks per 2*pi radians
    g_tOffA = static_cast<int>(t * 1.4f * 40.74f) & 0xFF;
    g_tOffB = static_cast<int>(t * 0.8f * 40.74f) & 0xFF;
    g_breatheQ8 = static_cast<int>((0.82f + 0.18f * fastSinRad(t * 0.45f)) * 256.0f);
}

// angle in Q8 turns (0..255 = full circle), via octant fold + minimax poly.
inline uint8_t fastAngleQ8(int dx, int dy) {
    const int ax = dx < 0 ? -dx : dx;
    const int ay = dy < 0 ? -dy : dy;
    const bool swap = ax < ay;
    const int hi = swap ? ay : ax;
    const int lo = swap ? ax : ay;
    if (hi == 0) {
        return 0;
    }
    // hi <= 240 always (center at 240,240), so recipLUT covers the range.
    const uint32_t ratioQ16 = (static_cast<uint32_t>(lo) * recipLUT[hi - 1]) >> 16;
    const float ratio = ratioQ16 * (1.0f / 65536.0f);
    const float ang = ratio * (0.9817f - 0.1963f * ratio * ratio); // radians, 0..pi/4
    int oct = static_cast<int>(ang * (128.0f / 3.14159265f));      // 0..32 within octant
    if (swap) {
        oct = 64 - oct;
    }
    int a;
    if (dx >= 0 && dy >= 0) {
        a = oct;
    } else if (dx < 0 && dy >= 0) {
        a = 128 - oct;
    } else if (dx < 0 && dy < 0) {
        a = 128 + oct;
    } else {
        a = 256 - oct;
    }
    return static_cast<uint8_t>(a);
}

void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const int cx = w / 2, cy = w / 2;
    const int maxR2 = cx * cx;
    for (int ry = 0; ry < rows; ry++) {
        const int y = y0 + ry;
        const int dy = y - cy;
        const int dy2 = dy * dy;
        uint16_t *row = dst + static_cast<size_t>(ry) * w;
        for (int x = 0; x < w; x++) {
            const int dx = x - cx;
            const int r2 = dx * dx + dy2;
            if (r2 > maxR2) {
                row[x] = g_outside;
                continue;
            }
            const int rbucket = r2 / 192;
            const int r = sqrtLUT[rbucket];
            const uint8_t angleQ8 = fastAngleQ8(dx, dy);
            const int rOffset = (r * g_rOffsetScale) & 0xFF;
            const uint8_t idxA = static_cast<uint8_t>(angleQ8 * g_N + rOffset + g_tOffA);
            const uint8_t idxB = static_cast<uint8_t>(angleQ8 * 2 * g_N - (rOffset * 3) / 5 + g_tOffB);
            int v = sin256[idxA] + (sin256[idxB] >> 1); // ~-190..190
            v = (v + 190) * 255 / 380;
            const int rIdx = (r * 255) / cx;
            v = (v * vigLUT[rIdx > 255 ? 255 : rIdx]) >> 8;
            v = (v * g_breatheQ8) >> 8;
            if (v < 0) {
                v = 0;
            } else if (v > 255) {
                v = 255;
            }
            row[x] = paletteLUT[v];
        }
    }
}

} // namespace

extern const BgAnimation bg_anim_mandala;
const BgAnimation bg_anim_mandala = {
    "mandala",
    "Mandala",
    {{"speed", "Speed", 50}, {"symmetry", "Symmetry", 50}, {"complexity", "Complexity", 45}, {nullptr, nullptr, 0}},
    init,
    frame,
    band,
};

#endif // GAGGIMATE_SIM
