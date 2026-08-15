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
int lastWarmth = -1;

struct Stop {
    uint8_t t, r, g, b;
};
constexpr Stop STOPS[6] = {{0, 0x05, 0x06, 0x0c},  {56, 0x0b, 0x18, 0x2c},  {115, 0x10, 0x2c, 0x44},
                           {168, 0x1f, 0x54, 0x54}, {209, 0xb0, 0x7a, 0x34}, {255, 0xf3, 0xda, 0xa3}};

void buildPaletteWarmth(uint8_t warmth) {
    // warmth shifts the sampling index by up to ±15 (±0.06 of the range).
    const int shift = ((warmth - 50) * 15) / 50;
    for (int i = 0; i < 256; i++) {
        int idx = i + shift;
        if (idx < 0) {
            idx = 0;
        } else if (idx > 255) {
            idx = 255;
        }
        int seg = 0;
        while (seg < 4 && STOPS[seg + 1].t < idx) {
            seg++;
        }
        float span = STOPS[seg + 1].t - STOPS[seg].t;
        if (span < 1) {
            span = 1;
        }
        const float f = (idx - STOPS[seg].t) / span;
        paletteLUT[i] = rgb565(static_cast<uint8_t>(STOPS[seg].r + (STOPS[seg + 1].r - STOPS[seg].r) * f),
                               static_cast<uint8_t>(STOPS[seg].g + (STOPS[seg + 1].g - STOPS[seg].g) * f),
                               static_cast<uint8_t>(STOPS[seg].b + (STOPS[seg + 1].b - STOPS[seg].b) * f));
    }
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
        buildPaletteWarmth(50);
        lastWarmth = 50;
    }
    return true;
}

int g_N = 8, g_tOffA = 0, g_tOffB = 0, g_rOffsetScale = 0;
int g_breatheQ8 = 256;

void frame(uint32_t tMs, int, int, const uint8_t p[4]) {
    if (p[3] != lastWarmth) {
        buildPaletteWarmth(p[3]);
        lastWarmth = p[3];
    }
    g_N = 4 + (p[0] * 8) / 100;
    const float t = tMs * 0.001f * (0.12f + (p[2] / 100.0f) * 0.55f);
    const float turb = 0.25f + (p[1] / 100.0f) * 1.1f;
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
                row[x] = rgb565(2, 2, 4);
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
    {{"symmetry", "Symmetry", 50}, {"complexity", "Complexity", 45}, {"speed", "Speed", 35}, {"warmth", "Warmth", 50}},
    init,
    frame,
    band,
};

#endif // GAGGIMATE_SIM
