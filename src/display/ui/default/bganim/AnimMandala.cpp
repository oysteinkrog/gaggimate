#ifndef GAGGIMATE_SIM

// "Mandala" — N-fold rotational symmetry built from angular harmonics
// (sin(N*theta)), which are smooth and periodic by construction — no fold
// seams. Design: anim-geometric (Fable), 2026-08-15.
//
// Polar map optimization (2026-08-15): angle and radius are geometry-only —
// they never depend on time or params — so instead of running the fast
// atan2 poly + sqrt-bucket lookup per pixel per frame, we compute them once
// in init() into a quadrant-symmetric map and turn band() into table reads.
// The disc is mirrored across both axes (dx,dy -> |dx|,|dy|), so the map
// only needs one quadrant: (cx+1)^2 entries for a 480-wide panel (cx=240)
// is ~58k uint16 entries, ~115 KB. Each entry packs:
//   bits 15..8: angleOct  — the octant-folded angle (0..64) for the point
//               (|dx|,|dy|), i.e. what fastAngleQ8 would return for a point
//               in the first quadrant (dx>=0, dy>=0).
//   bits 7..0:  radius (0..cx) if inside the inscribed circle, else the
//               sentinel 0xFF ("outside").
// band() looks up the quadrant entry via (|dx|,|dy|), then reconstructs the
// full 0..255 angle from angleOct + the two sign bits of (dx,dy) — the same
// case split fastAngleQ8 used to do per pixel, now just 2 compares + an add.
//
// Everything that is a function of radius alone but still depends on
// per-frame params (the radial phase offset `rOffset = (r*g_rOffsetScale)
// & 0xFF` folded into each harmonic's index, and the vignette x breathe
// scale) is baked into small (cx+1)-entry tables rebuilt once per frame in
// frame() — so band() never multiplies/divides by a per-frame param, it
// just adds two table reads together. The final (v+190)*255/380 rescale
// (v is bounded -190..190 by construction) is likewise a fixed one-time
// 381-entry LUT built in init(). Net per pixel: a handful of table reads,
// a couple of adds/shifts, one angle multiply — no float, no divide, no
// atan2 poly, no per-frame-param multiply.
#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

namespace {
using namespace bganim;

int16_t *sin256 = nullptr;      // Q7 sine, 256 entries
int16_t *sqrtLUT = nullptr;     // r2 bucket -> radius (bucket 192); init-time only
uint32_t *recipLUT = nullptr;   // Q16 65536/(i+1), 0..240; init-time only
uint8_t *vigByR = nullptr;      // radius 0..cx -> vignette falloff Q8, indexed directly by radius
uint8_t *rescaleLUT = nullptr;  // (v+190) in 0..380 -> 0..255, replaces the old */380 divide
uint16_t *paletteLUT = nullptr;
uint16_t *polarMap = nullptr;   // quadrant map: (angleOct<<8 | radiusOr0xFF), (cx+1)x(cx+1)
uint8_t *idxOffA = nullptr;     // per-frame: radius -> (rOffset + g_tOffA) & 0xFF
uint8_t *idxOffB = nullptr;     // per-frame: radius -> (g_tOffB - rOffset*3/5) & 0xFF
uint8_t *vigBreathe = nullptr;  // per-frame: radius -> (vigByR[r] * g_breatheQ8) >> 8
uint32_t lastThemeGen = 0xFFFFFFFF;
uint16_t g_outside = 0;
int g_cx = 240; // half panel width; also the map's per-axis extent (assumes cx < 255)

constexpr uint8_t OUTSIDE_R = 0xFF;

void buildThemePalette() {
    buildThemeRamp(paletteLUT, 256);
    uint8_t c[3];
    themeRGB(0, c);
    g_outside = rgb565(c[0], c[1], c[2]);
}

// Octant-folded angle (0..64) for a point in the first quadrant (ax,ay >= 0).
// Same reciprocal-LUT + minimax-poly approximation the old per-pixel path
// used, but now only ever called (cx+1)^2 times, once, in init().
inline uint8_t octantAngle(int ax, int ay) {
    const bool swap = ax < ay;
    const int hi = swap ? ay : ax;
    const int lo = swap ? ax : ay;
    if (hi == 0) {
        return 0;
    }
    // hi <= cx (<=240 for the real panel), recipLUT covers that range.
    const uint32_t ratioQ16 = (static_cast<uint32_t>(lo) * recipLUT[hi - 1]) >> 16;
    const float ratio = ratioQ16 * (1.0f / 65536.0f);
    const float ang = ratio * (0.9817f - 0.1963f * ratio * ratio); // radians, 0..pi/4
    int oct = static_cast<int>(ang * (128.0f / 3.14159265f));      // 0..32 within octant
    if (swap) {
        oct = 64 - oct;
    }
    return static_cast<uint8_t>(oct);
}

bool init(int w, int) {
    if (sin256 == nullptr) {
        g_cx = w / 2;
        const int mapDim = g_cx + 1;
        sin256 = static_cast<int16_t *>(alloc(256 * sizeof(int16_t)));
        sqrtLUT = static_cast<int16_t *>(alloc(602 * sizeof(int16_t)));
        recipLUT = static_cast<uint32_t *>(alloc(241 * sizeof(uint32_t)));
        vigByR = static_cast<uint8_t *>(alloc(mapDim));
        rescaleLUT = static_cast<uint8_t *>(alloc(381));
        paletteLUT = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
        polarMap = static_cast<uint16_t *>(alloc(static_cast<size_t>(mapDim) * mapDim * sizeof(uint16_t)));
        idxOffA = static_cast<uint8_t *>(alloc(mapDim));
        idxOffB = static_cast<uint8_t *>(alloc(mapDim));
        vigBreathe = static_cast<uint8_t *>(alloc(mapDim));
        if (sin256 == nullptr || sqrtLUT == nullptr || recipLUT == nullptr || vigByR == nullptr ||
            rescaleLUT == nullptr || paletteLUT == nullptr || polarMap == nullptr || idxOffA == nullptr ||
            idxOffB == nullptr || vigBreathe == nullptr) {
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
        for (int r = 0; r < mapDim; r++) {
            vigByR[r] = static_cast<uint8_t>(lroundf(255.0f * powf(1.0f - static_cast<float>(r) / g_cx, 0.55f)));
        }
        // v (sinA + sinB/2) is bounded to roughly -190..190 by construction
        // (|sinA|<=127, |sinB|<=127 so |sinB>>1|<=63); (v+190) in 0..380.
        for (int i = 0; i < 381; i++) {
            rescaleLUT[i] = static_cast<uint8_t>((i * 255) / 380);
        }
        const int maxR2 = g_cx * g_cx;
        for (int ay = 0; ay < mapDim; ay++) {
            for (int ax = 0; ax < mapDim; ax++) {
                const int r2 = ax * ax + ay * ay;
                uint8_t rOrOut;
                if (r2 > maxR2) {
                    rOrOut = OUTSIDE_R;
                } else {
                    rOrOut = static_cast<uint8_t>(sqrtLUT[r2 / 192]);
                }
                const uint8_t oct = octantAngle(ax, ay);
                polarMap[ay * mapDim + ax] = static_cast<uint16_t>((static_cast<uint16_t>(oct) << 8) | rOrOut);
            }
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

    // Fold everything that is "per-frame param x radius" but not per-pixel
    // into (cx+1)-entry tables — band() then just reads and adds. Cheap:
    // g_cx+1 (<=241) iterations, once per frame, plain integer ops.
    const int mapDim = g_cx + 1;
    for (int r = 0; r < mapDim; r++) {
        const int rOffset = (r * g_rOffsetScale) & 0xFF;
        idxOffA[r] = static_cast<uint8_t>(rOffset + g_tOffA);
        idxOffB[r] = static_cast<uint8_t>(g_tOffB - (rOffset * 3) / 5);
        vigBreathe[r] = static_cast<uint8_t>((vigByR[r] * g_breatheQ8) >> 8);
    }
}

// Reconstruct the full 0..255 angle from the quadrant-folded octant angle
// (0..64) plus the sign bits of dx,dy — mirrors the branch fastAngleQ8 used
// to take per pixel; the map already did the expensive part.
inline uint8_t foldAngle(uint8_t oct, bool sx, bool sy) {
    if (!sy) {
        return sx ? static_cast<uint8_t>(128 - oct) : oct;
    }
    return sx ? static_cast<uint8_t>(128 + oct) : static_cast<uint8_t>(256 - oct);
}

void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const int cx = w / 2, cy = w / 2;
    const int mapDim = g_cx + 1;
    for (int ry = 0; ry < rows; ry++) {
        const int y = y0 + ry;
        const int dy = y - cy;
        const int ay = dy < 0 ? -dy : dy; // ay in [0, g_cx] given w == 2*g_cx (band's contract)
        uint16_t *row = dst + static_cast<size_t>(ry) * w;
        const bool sy = dy < 0;
        const uint16_t *mapRow = polarMap + static_cast<size_t>(ay) * mapDim;
        for (int x = 0; x < w; x++) {
            const int dx = x - cx;
            const int ax = dx < 0 ? -dx : dx; // ax in [0, g_cx], same contract
            const uint16_t entry = mapRow[ax];
            const uint8_t rOrOut = entry & 0xFF;
            if (rOrOut == OUTSIDE_R) {
                row[x] = g_outside;
                continue;
            }
            const uint8_t oct = static_cast<uint8_t>(entry >> 8);
            const uint8_t angleQ8 = foldAngle(oct, dx < 0, sy);
            const int r = rOrOut;
            const int base = angleQ8 * g_N;
            const uint8_t idxA = static_cast<uint8_t>(base + idxOffA[r]);
            const uint8_t idxB = static_cast<uint8_t>(2 * base + idxOffB[r]);
            int v = sin256[idxA] + (sin256[idxB] >> 1); // ~-190..190
            v = rescaleLUT[v + 190];
            v = (v * vigBreathe[r]) >> 8;
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
