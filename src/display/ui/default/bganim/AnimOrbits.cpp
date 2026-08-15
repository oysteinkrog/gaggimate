#ifndef GAGGIMATE_SIM

// "Orbits" — 3-6 faint elliptical paths with glowing bodies at golden-ratio
// periods and analytic (recomputed, not feedback) fading trails. Sparse
// renderer: flat background, precomputed per-band path points, small
// coverage-AA stamps. Design: anim-geometric (Fable), 2026-08-15.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>
#include <string.h>

namespace {
using namespace bganim;

constexpr int MAX_ORBITS = 6;
constexpr int NUM_BANDS = 30;
constexpr int PTS_PER_BAND = 32;
constexpr float GOLDEN = 0.6180339887f;

struct OrbitDef {
    float a, b, phi, T, phase;
    float cosPhi, sinPhi;
    uint8_t colR, colG, colB;
    uint16_t pathColor565;
};

struct PathPt {
    int16_t x, y;
};

OrbitDef orbits[MAX_ORBITS];
int orbitCount = 0;
PathPt *pathBins = nullptr;   // [orbit][band][pt]
uint8_t *pathBinCount = nullptr; // [orbit][band]
int lastCountP = -1, lastEccP = -1;
uint32_t lastThemeGen = 0xFFFFFFFF;
int g_w = 480;
uint16_t g_bg = 0;

// Per-frame body + trail samples, computed in frame(), drawn per band.
struct Sample {
    float x, y, radius, alpha;
    uint8_t orbit;
};
constexpr int MAX_SAMPLES = MAX_ORBITS * 18;
Sample samples[MAX_SAMPLES];
int sampleCount = 0;

void rebuildGeometry(int countP, int eccP, int w, int h) {
    orbitCount = 3 + (countP * 3) / 100;
    const float bRatio = 0.95f - (eccP / 100.0f) * 0.4f;
    const float maxR = (w < h ? w : h) * 0.46f;
    const float cx = w * 0.5f, cy = h * 0.5f;
    memset(pathBinCount, 0, MAX_ORBITS * NUM_BANDS);
    uint8_t bgC[3];
    themeRGB(3, bgC);
    g_bg = rgb565(bgC[0], bgC[1], bgC[2]);
    const uint16_t bg = g_bg;
    for (int i = 0; i < orbitCount; i++) {
        OrbitDef &o = orbits[i];
        o.a = maxR * (0.30f + i * (0.62f / (orbitCount - 1)));
        o.b = o.a * bRatio;
        o.phi = i * 2.4f;
        o.cosPhi = cosf(o.phi);
        o.sinPhi = sinf(o.phi);
        o.T = 6.0f * powf(1.0f + GOLDEN, static_cast<float>(i));
        o.phase = i * 1.7f;
        // Bodies sample the theme's upper range, spread so neighbors differ.
        uint8_t col[3];
        themeRGB(140 + (i * 115) / (MAX_ORBITS - 1), col);
        o.colR = col[0];
        o.colG = col[1];
        o.colB = col[2];
        o.pathColor565 = blendQ8(bg, rgb565(o.colR, o.colG, o.colB), static_cast<int>(0.11f * 256));
        for (int s = 0; s < 480; s++) {
            const float u = s / 480.0f * 6.2831853f;
            const float cu = cosf(u), su = sinf(u);
            const float x = cx + o.a * cu * o.cosPhi - o.b * su * o.sinPhi;
            const float y = cy + o.a * cu * o.sinPhi + o.b * su * o.cosPhi;
            const int bandIdx = static_cast<int>(y) / 16;
            if (bandIdx < 0 || bandIdx >= NUM_BANDS || x < 0 || x >= w) {
                continue;
            }
            uint8_t &n = pathBinCount[i * NUM_BANDS + bandIdx];
            if (n < PTS_PER_BAND) {
                pathBins[(i * NUM_BANDS + bandIdx) * PTS_PER_BAND + n] = {static_cast<int16_t>(x), static_cast<int16_t>(y)};
                n++;
            }
        }
    }
}

bool init(int w, int h) {
    g_w = w;
    if (pathBins == nullptr) {
        pathBins = static_cast<PathPt *>(alloc(MAX_ORBITS * NUM_BANDS * PTS_PER_BAND * sizeof(PathPt)));
        pathBinCount = static_cast<uint8_t *>(alloc(MAX_ORBITS * NUM_BANDS));
        if (pathBins == nullptr || pathBinCount == nullptr) {
            return false;
        }
        rebuildGeometry(55, 55, w, h);
        lastCountP = 55;
        lastEccP = 55;
    }
    return true;
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[4]) {
    if (p[1] != lastCountP || p[2] != lastEccP || themeGen() != lastThemeGen) {
        rebuildGeometry(p[1], p[2], w, h);
        lastCountP = p[1];
        lastEccP = p[2];
        lastThemeGen = themeGen();
    }
    const float spd = speedMul(p[0]);
    const float trailAmt = p[3] / 100.0f;
    const int K = 6 + static_cast<int>(trailAmt * 10.0f);
    const float t = tMs * 0.001f;
    const float cx = w * 0.5f, cy = h * 0.5f;

    sampleCount = 0;
    for (int i = 0; i < orbitCount; i++) {
        const OrbitDef &o = orbits[i];
        const float T = o.T / spd;
        const float dt = T / 90.0f;
        for (int k = K; k >= 0; k--) {
            const float u = (t - k * dt) / T * 6.2831853f + o.phase;
            const float cu = fastCosRad(u), su = fastSinRad(u);
            const float ex = cx + o.a * cu * o.cosPhi - o.b * su * o.sinPhi;
            const float ey = cy + o.a * cu * o.sinPhi + o.b * su * o.cosPhi;
            const float f = 1.0f - static_cast<float>(k) / (K + 1);
            const float radius = (k == 0) ? 2.6f : 1.2f * f + 0.4f;
            float alpha = (k == 0) ? 0.95f : 0.55f * f * f * (0.4f + 0.8f * trailAmt); // f^2 ~ f^1.6, no powf
            if (sampleCount < MAX_SAMPLES) {
                samples[sampleCount++] = {ex, ey, radius, alpha, static_cast<uint8_t>(i)};
            }
        }
    }
}

void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const uint16_t bg = g_bg;
    const int total = rows * w;
    for (int i = 0; i < total; i++) {
        dst[i] = bg;
    }

    const int bandIdx = y0 / 16;
    for (int i = 0; i < orbitCount; i++) {
        const uint8_t n = pathBinCount[i * NUM_BANDS + bandIdx];
        const PathPt *pts = &pathBins[(i * NUM_BANDS + bandIdx) * PTS_PER_BAND];
        for (int j = 0; j < n; j++) {
            const int ly = pts[j].y - y0;
            if (ly >= 0 && ly < rows) {
                dst[static_cast<size_t>(ly) * w + pts[j].x] = orbits[i].pathColor565;
            }
        }
    }

    for (int s = 0; s < sampleCount; s++) {
        const Sample &sm = samples[s];
        const float reach = sm.radius; // coverage is zero beyond radius
        if (sm.y + reach < y0 || sm.y - reach >= y0 + rows) {
            continue;
        }
        const OrbitDef &o = orbits[sm.orbit];
        const int rr = static_cast<int>(ceilf(sm.radius));
        const int x0i = static_cast<int>(sm.x) - rr, y0i = static_cast<int>(sm.y) - rr;
        const float r2 = sm.radius * sm.radius; // cached: was recomputed per pixel
        const float invR = 1.0f / sm.radius;    // cached: one divide per sample, not per pixel
        for (int dy = 0; dy <= rr * 2 + 1; dy++) {
            const int yy = y0i + dy - y0;
            if (yy < 0 || yy >= rows) {
                continue;
            }
            for (int dx = 0; dx <= rr * 2 + 1; dx++) {
                const int xx = x0i + dx;
                if (xx < 0 || xx >= w) {
                    continue;
                }
                const float ddx = xx + 0.5f - sm.x, ddy = (y0i + dy) + 0.5f - sm.y;
                const float d2 = ddx * ddx + ddy * ddy;
                if (d2 >= r2) {
                    continue;
                }
                // Alpha-max/beta-min distance approximation (coeffs 0.9604/
                // 0.3978, max error ~4%) instead of sqrtf(d2): the stamp is a
                // 2-3px soft glow blob, so a few-percent wobble on the AA
                // fringe is imperceptible and this is the only per-pixel cost
                // left in the whole animation.
                const float adx = ddx < 0 ? -ddx : ddx, ady = ddy < 0 ? -ddy : ddy;
                const float dist = adx > ady ? (0.9604f * adx + 0.3978f * ady) : (0.9604f * ady + 0.3978f * adx);
                const float cov = 1.0f - dist * invR;
                if (cov <= 0.0f) {
                    continue;
                }
                const int aQ8 = static_cast<int>(sm.alpha * cov * cov * 256.0f);
                if (aQ8 <= 0) {
                    continue;
                }
                uint16_t &px = dst[static_cast<size_t>(yy) * w + xx];
                px = blendQ8(px, rgb565(o.colR, o.colG, o.colB), aQ8 > 256 ? 256 : aQ8);
            }
        }
    }
}

} // namespace

extern const BgAnimation bg_anim_orbits;
const BgAnimation bg_anim_orbits = {
    "orbits",
    "Orbits",
    {{"speed", "Speed", 50}, {"orbitCount", "Orbits", 55}, {"eccentricity", "Eccentricity", 55}, {"trail", "Trail", 50}},
    init,
    frame,
    band,
};

#endif // GAGGIMATE_SIM
