#ifndef GAGGIMATE_SIM

// "Steam" — rising wisps built from chains of overlapping soft blobs; each
// blob's position/radius/alpha is a pure function of its age (analytic, no
// feedback buffer), with a 4L(1-L) lifecycle envelope so nothing pops.
// Design: anim-particles (Fable), 2026-08-15.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

namespace {
using namespace bganim;

constexpr int WISPS_MAX = 5;
constexpr int BLOBS_PER_WISP = 11;
constexpr int BLOBS_MAX = WISPS_MAX * BLOBS_PER_WISP;

struct Wisp {
    float x0, y0;
    float swayPhase1, swayPhase2, swayFreq1, swayFreq2;
};
struct Blob {
    uint8_t wisp;
    uint32_t birth;
    float lifetime;
    float seed;
};
struct BlobDraw {
    float x, y, R, invR2;
    uint8_t a8, r, g, b;
    bool visible;
};

Wisp wisps[WISPS_MAX];
Blob blobs[BLOBS_MAX];
BlobDraw draws[BLOBS_MAX];
uint8_t *alphaLUT = nullptr; // (1-sqrt(i/63))^1.6
uint16_t *bgLUT = nullptr;
int wispCount = 0;
int builtCount = -1;
uint32_t rng = 0x1234abcd;
int g_active = 0;

void buildWisps(int count, int w, int h, uint32_t tMs) {
    const float cx = w * 0.5f, cy = h * 0.5f;
    const float rDisp = (w < h ? w : h) * 0.5f;
    const float y0 = h * 0.90f;
    const float dy = y0 - cy;
    const float halfSpan = sqrtf(fmaxf(0.0f, rDisp * rDisp - dy * dy)) * 0.7f;
    for (int i = 0; i < count; i++) {
        wisps[i] = {cx + (nextRandf(rng) * 2.0f - 1.0f) * halfSpan,
                    y0,
                    nextRandf(rng) * 6.2831853f,
                    nextRandf(rng) * 6.2831853f,
                    0.00045f + nextRandf(rng) * 0.0003f,
                    0.0013f + nextRandf(rng) * 0.0007f};
    }
    for (int i = 0; i < count; i++) {
        for (int k = 0; k < BLOBS_PER_WISP; k++) {
            const int idx = i * BLOBS_PER_WISP + k;
            const float lifetime = 4200.0f + nextRandf(rng) * 1800.0f;
            blobs[idx] = {static_cast<uint8_t>(i), tMs - static_cast<uint32_t>(nextRandf(rng) * lifetime), lifetime,
                          nextRandf(rng) * 6.2831853f};
        }
    }
    builtCount = count;
}

bool init(int, int h) {
    if (alphaLUT == nullptr) {
        alphaLUT = static_cast<uint8_t *>(alloc(64));
        bgLUT = static_cast<uint16_t *>(alloc(h * sizeof(uint16_t)));
        if (alphaLUT == nullptr || bgLUT == nullptr) {
            return false;
        }
        for (int i = 0; i < 64; i++) {
            const float norm = sqrtf(i / 63.0f);
            const float a = powf(fmaxf(0.0f, 1.0f - norm), 1.6f);
            alphaLUT[i] = static_cast<uint8_t>(a * 255.0f);
        }
        for (int y = 0; y < h; y++) {
            const float n = fabsf(y - h * 0.5f) / (h * 0.5f);
            bgLUT[y] = rgb565(static_cast<uint8_t>(16 + (6 - 16) * n), static_cast<uint8_t>(11 + (4 - 11) * n),
                              static_cast<uint8_t>(8 + (3 - 8) * n));
        }
    }
    return true;
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[4]) {
    const int count = 2 + (p[0] * 3) / 100;
    if (count != builtCount) {
        buildWisps(count, w, h, tMs);
    }
    wispCount = count;
    const float riseSpeed = 0.018f + (p[1] / 100.0f) * 0.032f;
    const float swirl = 0.5f + (p[2] / 100.0f) * 1.7f;
    const float density = 0.5f + (p[3] / 100.0f) * 0.8f;
    const float maxHeight = h * 0.62f;

    g_active = wispCount * BLOBS_PER_WISP;
    for (int i = 0; i < g_active; i++) {
        Blob &b = blobs[i];
        BlobDraw &d = draws[i];
        float age = static_cast<float>(tMs - b.birth);
        if (age > b.lifetime) {
            b.birth = tMs - static_cast<uint32_t>(fmodf(age, b.lifetime));
            age = static_cast<float>(tMs - b.birth);
        }
        const float L = age / b.lifetime;
        const Wisp &wp = wisps[b.wisp];
        const float rise = riseSpeed * age;
        const float heightFrac = fminf(1.0f, rise / maxHeight);
        d.y = wp.y0 - rise;
        d.R = (10.0f + 26.0f * heightFrac) * (0.85f + 0.3f * fastSinRad(b.seed));
        const float swayAmp = (5.0f + 22.0f * heightFrac) * swirl;
        d.x = wp.x0 + swayAmp * fastSinRad(wp.swayFreq1 * tMs + wp.swayPhase1 + b.seed) +
              swayAmp * 0.35f * fastSinRad(wp.swayFreq2 * tMs + wp.swayPhase2 + b.seed * 1.7f);
        float alpha = density * 0.44f * 4.0f * L * (1.0f - L) * (1.0f - heightFrac * 0.3f);
        if (alpha > 0.4f) {
            alpha = 0.4f; // hard cap: steam stays vapor, never opaque
        }
        d.a8 = static_cast<uint8_t>(alpha * 255.0f);
        d.invR2 = 1.0f / (d.R * d.R);
        d.r = static_cast<uint8_t>(214 + (206 - 214) * heightFrac);
        d.g = static_cast<uint8_t>(178 + (210 - 178) * heightFrac);
        d.b = static_cast<uint8_t>(140 + (216 - 140) * heightFrac);
        d.visible = d.a8 > 0 && d.y > -30.0f;
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
    for (int i = 0; i < g_active; i++) {
        const BlobDraw &d = draws[i];
        if (!d.visible || d.y + d.R < y0 || d.y - d.R >= y0 + rows) {
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

extern const BgAnimation bg_anim_steam;
const BgAnimation bg_anim_steam = {
    "steam",
    "Steam",
    {{"count", "Wisps", 55}, {"riseSpeed", "Rise speed", 50}, {"swirl", "Swirl", 45}, {"density", "Density", 50}},
    init,
    frame,
    band,
};

#endif // GAGGIMATE_SIM
