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

struct FfDraw {
    float x, y, R, invR2;
    uint8_t a8;
    uint8_t r, g, b;
};

Firefly *ff = nullptr;
FfDraw *draws = nullptr;
uint8_t *alphaLUT = nullptr; // 64 entries, indexed by normalized d^2
uint16_t *bgLUT = nullptr;   // per-scanline background
int ffCount = 0;
int builtCount = -1;
uint32_t rng = 0x9e3779b9;

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
        if (ff == nullptr || draws == nullptr || alphaLUT == nullptr || bgLUT == nullptr) {
            return false;
        }
        for (int i = 0; i < 64; i++) {
            float a = 1.0f - sqrtf(i / 63.0f);
            a = a < 0 ? 0 : a * a;
            alphaLUT[i] = static_cast<uint8_t>(a * 255.0f);
        }
        for (int y = 0; y < h; y++) {
            const float n = fabsf(y - h * 0.5f) / (h * 0.5f);
            bgLUT[y] = rgb565(static_cast<uint8_t>(7 + (3 - 7) * n), static_cast<uint8_t>(14 + (6 - 14) * n),
                              static_cast<uint8_t>(20 + (10 - 20) * n));
        }
    }
    return true;
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[4]) {
    const int count = 15 + (p[0] * 25) / 100;
    if (count != builtCount) {
        spawnAll(count, w, h);
    }
    ffCount = count;
    const float speed = 0.4f + (p[1] / 100.0f) * 1.2f;
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
        d.invR2 = 1.0f / (d.R * d.R);
        float pulse = fastSinRad(f.pulseFreq * t + f.pulsePhase);
        pulse = pulse < 0 ? 0 : pulse * pulse;
        float brightness = 0.28f + 0.72f * pulse;
        if (shimAmt > 0) {
            const float dr = f.radialNorm - ringPos;
            brightness += shimAmt * expf(-(dr * dr) * 61.7f); // sigma 0.09
        }
        d.a8 = brightness >= 1.0f ? 255 : static_cast<uint8_t>(brightness * 255.0f);
        // Warm core blended toward green glow per particle (hueMix), computed
        // once per frame instead of per pixel.
        const float mix = f.hueMix;
        d.r = clamp8f(255.0f * (1.0f - mix * 0.25f));
        d.g = clamp8f(230.0f + mix * 0.0f);
        d.b = clamp8f(130.0f - mix * 40.0f);
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
            const float dy = yy - d.y;
            const float dy2 = dy * dy;
            uint16_t *row = dst + static_cast<size_t>(yy - y0) * w;
            for (int xx = xx0; xx <= xx1; xx++) {
                const float dx = xx - d.x;
                const int idx = static_cast<int>((dx * dx + dy2) * d.invR2 * 63.0f);
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
    {{"count", "Count", 60}, {"speed", "Speed", 50}, {"glow", "Glow", 55}, {"shimmer", "Shimmer", 40}},
    init,
    frame,
    band,
};

#endif // GAGGIMATE_SIM
