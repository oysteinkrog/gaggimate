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
    // Fixed-point draw state (all set once per frame in frame(), consumed
    // per-pixel in band()). xi/yi/Ri are rounded pixel-space ints; scaleQ is
    // a Q16.16 factor so idx = ((dx*dx+dy2)*scaleQ) >> 16 reproduces
    // (dist^2 * invR2 * 63) without any float ops in the band inner loop.
    // Bounded: (dx*dx+dy2) <= 2*Ri*Ri inside the bbox, so the product never
    // exceeds 2*63*65536 (~8.3M) regardless of R -- always safe in int32.
    int xi, yi, Ri;
    int32_t scaleQ;
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
uint32_t lastThemeGen = 0xFFFFFFFF;
int g_h = 480;

void rebuildBg() {
    for (int y = 0; y < g_h; y++) {
        const float n = fabsf(y - g_h * 0.5f) / (g_h * 0.5f);
        uint8_t c[3];
        themeRGB(static_cast<int>(9.0f - n * 6.0f), c);
        bgLUT[y] = rgb565(c[0], c[1], c[2]);
    }
}

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
        g_h = h;
        for (int i = 0; i < 64; i++) {
            const float norm = sqrtf(i / 63.0f);
            const float a = powf(fmaxf(0.0f, 1.0f - norm), 1.6f);
            alphaLUT[i] = static_cast<uint8_t>(a * 255.0f);
        }
        rebuildBg();
        lastThemeGen = themeGen();
    }
    return true;
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[4]) {
    if (themeGen() != lastThemeGen) {
        rebuildBg();
        lastThemeGen = themeGen();
    }
    const int count = 2 + (p[1] * 3) / 100;
    if (count != builtCount) {
        buildWisps(count, w, h, tMs);
    }
    wispCount = count;
    const float riseSpeed = 0.034f * speedMul(p[0]);
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
        const float y = wp.y0 - rise;
        const float R = (10.0f + 26.0f * heightFrac) * (0.85f + 0.3f * fastSinRad(b.seed));
        const float swayAmp = (5.0f + 22.0f * heightFrac) * swirl;
        const float x = wp.x0 + swayAmp * fastSinRad(wp.swayFreq1 * tMs + wp.swayPhase1 + b.seed) +
                        swayAmp * 0.35f * fastSinRad(wp.swayFreq2 * tMs + wp.swayPhase2 + b.seed * 1.7f);
        float alpha = density * 0.44f * 4.0f * L * (1.0f - L) * (1.0f - heightFrac * 0.3f);
        if (alpha > 0.4f) {
            alpha = 0.4f; // hard cap: steam stays vapor, never opaque
        }
        d.a8 = static_cast<uint8_t>(alpha * 255.0f);
        // Round to nearest pixel/Q16.16 once per blob per frame (cheap:
        // <=55 blobs/frame); the band loop below then stays all-integer.
        d.xi = static_cast<int>(x >= 0.0f ? x + 0.5f : x - 0.5f);
        d.yi = static_cast<int>(y >= 0.0f ? y + 0.5f : y - 0.5f);
        d.Ri = static_cast<int>(R + 0.5f);
        d.scaleQ = static_cast<int32_t>(63.0f * 65536.0f / (R * R) + 0.5f);
        // Wisps ride the theme's bright end, shifting slightly as they rise.
        uint8_t c[3];
        themeRGB(200 + static_cast<int>(heightFrac * 55.0f), c);
        d.r = c[0];
        d.g = c[1];
        d.b = c[2];
        d.visible = d.a8 > 0 && y > -30.0f;
    }
}

void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int r = 0; r < rows; r++) {
        const uint16_t c = bgLUT[y0 + r];
        uint16_t *row = dst + static_cast<size_t>(r) * w;
        // Fill two pixels per store: the band buffer is 4-byte aligned and
        // w is even (480), so pairing halves store traffic vs. one s16i/px.
        // Odd-width tail (defensive; never hit at w=480) falls back to a
        // single 16-bit store.
        const uint32_t c2 = (static_cast<uint32_t>(c) << 16) | c;
        uint32_t *row32 = reinterpret_cast<uint32_t *>(row);
        const int pairs = w >> 1;
        for (int x = 0; x < pairs; x++) {
            row32[x] = c2;
        }
        if (w & 1) {
            row[w - 1] = c;
        }
    }
    for (int i = 0; i < g_active; i++) {
        const BlobDraw &d = draws[i];
        if (!d.visible || d.yi + d.Ri < y0 || d.yi - d.Ri >= y0 + rows) {
            continue;
        }
        const int yy0 = d.yi - d.Ri > y0 ? d.yi - d.Ri : y0;
        const int yy1 = d.yi + d.Ri < y0 + rows - 1 ? d.yi + d.Ri : y0 + rows - 1;
        const int xx0 = d.xi - d.Ri > 0 ? d.xi - d.Ri : 0;
        const int xx1 = d.xi + d.Ri < w - 1 ? d.xi + d.Ri : w - 1;
        for (int yy = yy0; yy <= yy1; yy++) {
            const int dy = yy - d.yi;
            const int dy2 = dy * dy;
            uint16_t *row = dst + static_cast<size_t>(yy - y0) * w;
            for (int xx = xx0; xx <= xx1; xx++) {
                const int dx = xx - d.xi;
                // All-integer stamp lookup: (dx*dx+dy2) <= 2*Ri*Ri inside
                // this bbox, so the product with scaleQ (Q16.16) never
                // overflows int32 -- see BlobDraw comment.
                const int32_t idx = ((dx * dx + dy2) * d.scaleQ) >> 16;
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
    {{"speed", "Rise speed", 50}, {"count", "Wisps", 55}, {"swirl", "Swirl", 45}, {"density", "Density", 50}},
    init,
    frame,
    band,
};

#endif // GAGGIMATE_SIM
