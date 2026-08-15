#ifndef GAGGIMATE_SIM

// "Fireflies" — soft motes on sum-of-sines wander paths with individual
// pulses and an occasional synchronized shimmer ring. Sparse additive glow
// sprites over a per-scanline gradient. Design: anim-particles (Fable),
// 2026-08-15.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

namespace {
using namespace bganim;

constexpr int FF_MAX = 40;

struct Firefly {
    float homeX, homeY;
    float wx1, wx2, wy1, wy2;
    float ax1, ax2, ay1, ay2;
    float px1, px2, py1, py2;
    float pulseFreq, pulsePhase;
    float radialNorm;
    float size;
    float hueMix;
};

// Per-pixel falloff index is computed in fixed point. The sprite center is
// kept as Q8.8 (cxQ8/cyQ8, 1/256 px precision) rather than rounded to a whole
// pixel: near the sprite core alphaLUT steps hard (255 -> ~195 from index 0
// to 1), so snapping the center to an integer pixel shifts which pixel
// straddles that boundary and produces a visible one-pixel flicker relative
// to the float reference. Q8 sub-pixel precision keeps the boundary in the
// same place the float math would put it.
//   dxQ8 = (px<<8) - cxQ8                          (Q8, px - x)
//   dx2  = (dxQ8*dxQ8) >> 16                        (back to px^2 units)
//   idx  = ((dx2+dy2) * invR2Fixed) >> 16            (Q16.16 * invR2*63)
// invR2Fixed is bounded: (dx2+dy2) <= 2*R*R within the bounding box, so the
// product (2*R*R) * (63<<16)/(R*R) == 2*63*65536 ~= 8.26M regardless of R,
// well inside int32 range for any firefly size.
struct FfDraw {
    float x, y, R;      // R kept in float only for the per-firefly bbox calc
    int32_t cxQ8, cyQ8; // Q8.8 sub-pixel center
    int32_t invR2Fixed; // Q16.16, pre-scaled by the 63-entry alphaLUT span
    uint8_t a8;
    uint8_t r, g, b;
};

// Shimmer ring Gaussian LUT: expf(-(dr*dr)*61.7f) sampled uniformly in dr
// (not in dr*dr*61.7f — that domain is 64-wide but the curve's whole
// interesting structure sits inside dr in [-0.3, 0.3] i.e. a handful of
// sigmas (sigma=0.09), so uniform-x sampling wastes almost all its
// resolution on the flat near-zero tail). dr = radialNorm-ringPos in
// roughly [-1, 1]; sample |dr| in [0,1]. Built once in init(); frame() only
// ever indexes it, no libm per firefly.
constexpr int EXP_LUT_N = 256;
constexpr float EXP_LUT_DR_MAX = 1.0f;

Firefly *ff = nullptr;
FfDraw *draws = nullptr;
uint8_t *alphaLUT = nullptr; // 64 entries, indexed by normalized d^2
uint16_t *bgLUT = nullptr;   // per-scanline background
uint8_t *ffCol = nullptr;    // FF_MAX * 3, per-particle base color from the theme
float *expLUT = nullptr;     // EXP_LUT_N entries, expf(-(dr*dr)*61.7f) over |dr| in [0, EXP_LUT_DR_MAX)
int ffCount = 0;
int builtCount = -1;
uint32_t rng = 0x9e3779b9;
uint32_t lastThemeGen = 0xFFFFFFFF;
int g_h = 480;

// LUT replacement for expf(-(dr*dr)*61.7f), indexed directly by |dr|.
// dr magnitudes beyond the table domain contribute ~0 anyway.
inline float expLutLookup(float dr) {
    int idx = static_cast<int>(fabsf(dr) * (static_cast<float>(EXP_LUT_N - 1) / EXP_LUT_DR_MAX));
    if (idx >= EXP_LUT_N) {
        idx = EXP_LUT_N - 1;
    }
    return expLUT[idx];
}

// Particles glow in the theme's bright range (per-particle hueMix spreads
// them); the dusk background sits in the darkest few percent.
void rebuildThemeAssets() {
    for (int i = 0; i < FF_MAX; i++) {
        themeRGB(185 + static_cast<int>(ff[i].hueMix * 70.0f), &ffCol[i * 3]);
    }
    for (int y = 0; y < g_h; y++) {
        const float n = fabsf(y - g_h * 0.5f) / (g_h * 0.5f);
        uint8_t c[3];
        themeRGB(static_cast<int>(8.0f - n * 5.0f), c);
        bgLUT[y] = rgb565(c[0], c[1], c[2]);
    }
}

void spawnAll(int count, int w, int h) {
    const float cx = w * 0.5f, cy = h * 0.5f;
    const float rMax = (w < h ? w : h) * 0.46f;
    for (int i = 0; i < count; i++) {
        const float theta = nextRandf(rng) * 6.2831853f;
        const float rr = rMax * sqrtf(nextRandf(rng)) * 0.92f;
        Firefly &f = ff[i];
        f.homeX = cx + cosf(theta) * rr;
        f.homeY = cy + sinf(theta) * rr;
        const float basePeriod = 9000.0f + nextRandf(rng) * 5000.0f;
        f.wx1 = 6.2831853f / basePeriod;
        f.wx2 = f.wx1 * 1.618f * (0.85f + nextRandf(rng) * 0.3f);
        f.wy1 = f.wx1 * 1.13f * (0.9f + nextRandf(rng) * 0.2f);
        f.wy2 = f.wx1 * 1.414f * (0.85f + nextRandf(rng) * 0.3f);
        f.ax1 = 16.0f + nextRandf(rng) * 10.0f;
        f.ax2 = 7.0f + nextRandf(rng) * 6.0f;
        f.ay1 = 16.0f + nextRandf(rng) * 10.0f;
        f.ay2 = 7.0f + nextRandf(rng) * 6.0f;
        f.px1 = nextRandf(rng) * 6.2831853f;
        f.px2 = nextRandf(rng) * 6.2831853f;
        f.py1 = nextRandf(rng) * 6.2831853f;
        f.py2 = nextRandf(rng) * 6.2831853f;
        f.pulseFreq = 6.2831853f / (2400.0f + nextRandf(rng) * 3600.0f);
        f.pulsePhase = nextRandf(rng) * 6.2831853f;
        f.radialNorm = rr / rMax;
        f.size = 0.8f + nextRandf(rng) * 0.5f;
        f.hueMix = nextRandf(rng);
    }
    builtCount = count;
}

bool init(int w, int h) {
    if (ff == nullptr) {
        ff = static_cast<Firefly *>(alloc(FF_MAX * sizeof(Firefly)));
        draws = static_cast<FfDraw *>(alloc(FF_MAX * sizeof(FfDraw)));
        alphaLUT = static_cast<uint8_t *>(alloc(64));
        bgLUT = static_cast<uint16_t *>(alloc(h * sizeof(uint16_t)));
        ffCol = static_cast<uint8_t *>(alloc(FF_MAX * 3));
        expLUT = static_cast<float *>(alloc(EXP_LUT_N * sizeof(float)));
        if (ff == nullptr || draws == nullptr || alphaLUT == nullptr || bgLUT == nullptr || ffCol == nullptr ||
            expLUT == nullptr) {
            return false;
        }
        g_h = h;
        for (int i = 0; i < 64; i++) {
            float a = 1.0f - sqrtf(i / 63.0f);
            a = a < 0 ? 0 : a * a;
            alphaLUT[i] = static_cast<uint8_t>(a * 255.0f);
        }
        for (int i = 0; i < EXP_LUT_N; i++) {
            const float dr = i * (EXP_LUT_DR_MAX / (EXP_LUT_N - 1));
            expLUT[i] = expf(-(dr * dr) * 61.7f);
        }
    }
    return true;
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[4]) {
    const int count = 15 + (p[1] * 25) / 100;
    if (count != builtCount) {
        spawnAll(count, w, h);
        rebuildThemeAssets();
        lastThemeGen = themeGen();
    }
    if (themeGen() != lastThemeGen) {
        rebuildThemeAssets();
        lastThemeGen = themeGen();
    }
    ffCount = count;
    const float speed = speedMul(p[0]);
    const float glow = 0.7f + (p[2] / 100.0f) * 0.8f;
    const float shimAmt = p[3] / 100.0f;
    const float shimPeriod = 14000.0f - shimAmt * 8000.0f;
    const float t = tMs * speed;
    const float ringPos = fmodf(t, shimPeriod) / shimPeriod;

    for (int i = 0; i < ffCount; i++) {
        const Firefly &f = ff[i];
        FfDraw &d = draws[i];
        d.x = f.homeX + f.ax1 * fastSinRad(f.wx1 * t + f.px1) + f.ax2 * fastSinRad(f.wx2 * t + f.px2);
        d.y = f.homeY + f.ay1 * fastSinRad(f.wy1 * t + f.py1) + f.ay2 * fastSinRad(f.wy2 * t + f.py2);
        d.R = (6.0f + f.size * 8.0f) * glow;
        d.cxQ8 = static_cast<int32_t>(d.x * 256.0f + 0.5f);
        d.cyQ8 = static_cast<int32_t>(d.y * 256.0f + 0.5f);
        // Q16.16 scaled by the 63-entry alphaLUT span: idx = (dx*dx+dy*dy)*invR2Fixed >> 16.
        // Round (not truncate) here: truncating this reciprocal alone biases
        // every falloff index low, making every sprite render a hair larger
        // and brighter than the float reference (visible as a systematic,
        // not random, diff against golden).
        d.invR2Fixed = static_cast<int32_t>((63.0f * 65536.0f) / (d.R * d.R) + 0.5f);
        float pulse = fastSinRad(f.pulseFreq * t + f.pulsePhase);
        pulse = pulse < 0 ? 0 : pulse * pulse;
        float brightness = 0.28f + 0.72f * pulse;
        if (shimAmt > 0) {
            const float dr = f.radialNorm - ringPos;
            brightness += shimAmt * expLutLookup(dr); // sigma 0.09
        }
        d.a8 = brightness >= 1.0f ? 255 : static_cast<uint8_t>(brightness * 255.0f);
        d.r = ffCol[i * 3 + 0];
        d.g = ffCol[i * 3 + 1];
        d.b = ffCol[i * 3 + 2];
    }
}

void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int r = 0; r < rows; r++) {
        const uint16_t c = bgLUT[y0 + r];
        uint16_t *row = dst + static_cast<size_t>(r) * w;
        for (int x = 0; x < w; x++) {
            row[x] = c;
        }
    }
    for (int i = 0; i < ffCount; i++) {
        const FfDraw &d = draws[i];
        if (d.y + d.R < y0 || d.y - d.R >= y0 + rows) {
            continue;
        }
        const int yy0 = static_cast<int>(fmaxf(static_cast<float>(y0), d.y - d.R));
        const int yy1 = static_cast<int>(fminf(static_cast<float>(y0 + rows - 1), d.y + d.R));
        const int xx0 = static_cast<int>(fmaxf(0.0f, d.x - d.R));
        const int xx1 = static_cast<int>(fminf(static_cast<float>(w - 1), d.x + d.R));
        for (int yy = yy0; yy <= yy1; yy++) {
            const int32_t dyQ8 = (yy << 8) - d.cyQ8;
            const int32_t dy2Q4 = (dyQ8 * dyQ8) >> 12; // px^2 in Q4 (16ths) — see note above
            uint16_t *row = dst + static_cast<size_t>(yy - y0) * w;
            for (int xx = xx0; xx <= xx1; xx++) {
                const int32_t dxQ8 = (xx << 8) - d.cxQ8;
                const int32_t dx2Q4 = (dxQ8 * dxQ8) >> 12;
                const int idx = static_cast<int>(((dx2Q4 + dy2Q4) * d.invR2Fixed) >> 20);
                if (idx >= 64) {
                    continue;
                }
                const uint8_t a = (static_cast<uint16_t>(alphaLUT[idx]) * d.a8) >> 8;
                if (a == 0) {
                    continue;
                }
                row[xx] = addScaled565(row[xx], d.r, d.g, d.b, a);
            }
        }
    }
}

} // namespace

extern const BgAnimation bg_anim_fireflies;
const BgAnimation bg_anim_fireflies = {
    "fireflies",
    "Fireflies",
    {{"speed", "Speed", 50}, {"count", "Count", 60}, {"glow", "Glow", 55}, {"shimmer", "Shimmer", 40}},
    init,
    frame,
    band,
};

#endif // GAGGIMATE_SIM
