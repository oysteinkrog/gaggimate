#ifndef GAGGIMATE_SIM

// "Starfield" — deep-sky star drift with per-star twinkle and a rare shooting
// star. Per-frame float math runs over ~400 stars (cheap); the band renderer
// only fills the vignette and plots bucketed stars. Design: anim-celestial
// (Fable), 2026-08-15.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

namespace {
using namespace bganim;

constexpr int MAX_STARS = 400;
constexpr int NUM_BANDS = 30; // 480/16

struct Star {
    float x, y;
    float phase, rate;
    float baseBrightness;
    uint8_t sizeClass;
    float hue;
    float driftSpeed;
};

struct StarDraw {
    int16_t x;
    uint8_t r, g, b; // final 8-bit color this frame
    uint8_t sizeClass;
};

Star *stars = nullptr;
StarDraw *draws = nullptr;      // per star, this frame
int16_t *starY = nullptr;       // fixed row per star
int16_t *bandHead = nullptr;    // NUM_BANDS heads
int16_t *bandNext = nullptr;    // linked list per star
int32_t *dx2 = nullptr, *dy2 = nullptr;
uint8_t *vigLUT = nullptr; // 128 entries
int g_nStars = 0;

struct Shoot {
    bool active = false;
    float x, y, vx, vy, life, maxLife;
};
Shoot shoot;
uint32_t nextShootMs = 6000;
uint32_t rng = 0xC0FFEE;
uint32_t lastTMs = 0;

bool init(int w, int h) {
    if (stars == nullptr) {
        stars = static_cast<Star *>(alloc(MAX_STARS * sizeof(Star)));
        draws = static_cast<StarDraw *>(alloc(MAX_STARS * sizeof(StarDraw)));
        starY = static_cast<int16_t *>(alloc(MAX_STARS * sizeof(int16_t)));
        bandHead = static_cast<int16_t *>(alloc(NUM_BANDS * sizeof(int16_t)));
        bandNext = static_cast<int16_t *>(alloc(MAX_STARS * sizeof(int16_t)));
        dx2 = static_cast<int32_t *>(alloc(w * sizeof(int32_t)));
        dy2 = static_cast<int32_t *>(alloc(h * sizeof(int32_t)));
        vigLUT = static_cast<uint8_t *>(alloc(128));
        if (stars == nullptr || draws == nullptr || starY == nullptr || bandHead == nullptr || bandNext == nullptr ||
            dx2 == nullptr || dy2 == nullptr || vigLUT == nullptr) {
            return false;
        }
        const float cx = w * 0.5f, cy = h * 0.5f;
        for (int x = 0; x < w; x++) {
            const float d = x - cx;
            dx2[x] = static_cast<int32_t>(d * d);
        }
        for (int y = 0; y < h; y++) {
            const float d = y - cy;
            dy2[y] = static_cast<int32_t>(d * d);
        }
        const float maxR2 = cx * cx + cy * cy;
        for (int i = 0; i < 128; i++) {
            // index maps r2 linearly; shade = 1 - r/maxR
            const float rn = sqrtf(i / 127.0f * maxR2) / sqrtf(maxR2);
            vigLUT[i] = static_cast<uint8_t>(fmaxf(0.0f, 1.0f - rn) * 255.0f);
        }
        for (int i = 0; i < MAX_STARS; i++) {
            const float roll = nextRandf(rng);
            const uint8_t layer = roll < 0.7f ? 0 : (roll < 0.92f ? 1 : 2);
            stars[i].x = nextRandf(rng) * w;
            stars[i].y = nextRandf(rng) * h;
            stars[i].phase = nextRandf(rng) * 6.2831853f;
            stars[i].rate = 0.3f + nextRandf(rng) * 1.1f;
            stars[i].baseBrightness = layer == 0 ? (0.25f + nextRandf(rng) * 0.25f)
                                      : layer == 1 ? (0.45f + nextRandf(rng) * 0.25f)
                                                   : (0.7f + nextRandf(rng) * 0.3f);
            stars[i].sizeClass = layer;
            stars[i].hue = nextRandf(rng);
            stars[i].driftSpeed = 0.5f + nextRandf(rng);
            starY[i] = static_cast<int16_t>(stars[i].y);
        }
    }
    return true;
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[4]) {
    (void)h;
    g_nStars = 40 + (p[0] * (MAX_STARS - 40)) / 100;
    const float t = tMs * 0.001f;
    const float twinkleAmt = p[1] / 100.0f;
    const float driftPxPerSec = (p[3] / 100.0f) * 2.0f;

    for (int b = 0; b < NUM_BANDS; b++) {
        bandHead[b] = -1;
    }
    for (int i = 0; i < g_nStars; i++) {
        const Star &s = stars[i];
        float x = fmodf(s.x + t * driftPxPerSec * s.driftSpeed, static_cast<float>(w));
        if (x < 0) {
            x += w;
        }
        const float edgeFade = fminf(1.0f, fminf(x, w - x) / 20.0f);
        const float twinkle = 0.7f + 0.3f * fastSinRad(t * s.rate + s.phase);
        float bF = s.baseBrightness * (1.0f - twinkleAmt + twinkleAmt * twinkle) * edgeFade;
        bF = fmaxf(0.0f, fminf(1.0f, bF));
        draws[i].x = static_cast<int16_t>(x);
        draws[i].r = clamp8f((200.0f + s.hue * 55.0f) * bF);
        draws[i].g = clamp8f((210.0f + (0.5f - fabsf(s.hue - 0.5f)) * 30.0f) * bF);
        draws[i].b = clamp8f((255.0f - s.hue * 90.0f) * bF);
        draws[i].sizeClass = s.sizeClass;
        const int bandIdx = starY[i] >> 4;
        bandNext[i] = bandHead[bandIdx];
        bandHead[bandIdx] = static_cast<int16_t>(i);
    }

    // Shooting star lifecycle (dt from the frame delta; robust to pauses).
    const float dt = lastTMs != 0 && tMs > lastTMs ? (tMs - lastTMs) * 0.001f : 0.033f;
    lastTMs = tMs;
    const uint8_t shootFreq = p[2];
    if (!shoot.active && shootFreq > 0 && tMs > nextShootMs) {
        const float angle = 3.14159265f * 0.15f + nextRandf(rng) * 3.14159265f * 0.2f;
        const float speed = 260.0f + nextRandf(rng) * 140.0f;
        shoot.active = true;
        shoot.x = nextRandf(rng) * w * 0.6f;
        shoot.y = nextRandf(rng) * w * 0.3f;
        shoot.vx = cosf(angle) * speed;
        shoot.vy = sinf(angle) * speed;
        shoot.life = 0;
        shoot.maxLife = 0.5f + nextRandf(rng) * 0.3f;
        const uint32_t interval = 1500 > 30000 - shootFreq * 280 ? 1500 : 30000 - shootFreq * 280;
        nextShootMs = tMs + interval + static_cast<uint32_t>(nextRandf(rng) * interval * 0.5f);
    }
    if (shoot.active) {
        shoot.life += dt;
        if (shoot.life >= shoot.maxLife) {
            shoot.active = false;
        }
    }
}

inline void plotMax(uint16_t *dst, int rows, int w, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || x >= w || y < 0 || y >= rows) {
        return;
    }
    // Stars replace (max) rather than blend — background is near-black.
    uint16_t &px = dst[static_cast<size_t>(y) * w + x];
    const uint16_t c = rgb565(r, g, b);
    if (c > px) {
        px = c;
    }
}

void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    // 1. vignette background (squared-distance LUT, no sqrt)
    for (int ry = 0; ry < rows; ry++) {
        const int y = y0 + ry;
        const int32_t dyv = dy2[y];
        uint16_t *row = dst + static_cast<size_t>(ry) * w;
        for (int x = 0; x < w; x++) {
            const int32_t r2 = dyv + dx2[x];
            int idx = r2 >> 10; // 480x480: max r2 ~115200 -> 112
            if (idx > 127) {
                idx = 127;
            }
            const uint8_t shade = vigLUT[idx];
            row[x] = rgb565(2 + ((shade * 4) >> 8), 3 + ((shade * 6) >> 8), 8 + ((shade * 12) >> 8));
        }
    }
    // 2. stars bucketed for this band (plus neighbors for the 1px spill)
    int bandLo = (y0 >> 4) - 1, bandHi = ((y0 + rows - 1) >> 4) + 1;
    if (bandLo < 0) {
        bandLo = 0;
    }
    if (bandHi > NUM_BANDS - 1) {
        bandHi = NUM_BANDS - 1;
    }
    for (int b = bandLo; b <= bandHi; b++) {
        for (int16_t i = bandHead[b]; i >= 0; i = bandNext[i]) {
            const StarDraw &d = draws[i];
            const int y = starY[i] - y0;
            const int x = d.x;
            plotMax(dst, rows, w, x, y, d.r, d.g, d.b);
            if (d.sizeClass >= 1) {
                plotMax(dst, rows, w, x + 1, y, d.r * 2 / 5, d.g * 2 / 5, d.b * 2 / 5);
            }
            if (d.sizeClass >= 2) {
                plotMax(dst, rows, w, x, y + 1, d.r * 2 / 5, d.g * 2 / 5, d.b * 2 / 5);
                plotMax(dst, rows, w, x - 1, y, d.r * 3 / 10, d.g * 3 / 10, d.b * 3 / 10);
            }
        }
    }
    // 3. shooting star trail (14 segments, band-clipped by plotMax)
    if (shoot.active) {
        const float progress = shoot.life / shoot.maxLife;
        const float hx = shoot.x + shoot.vx * shoot.life;
        const float hy = shoot.y + shoot.vy * shoot.life;
        for (int k = 0; k < 14; k++) {
            const float f = k / 14.0f;
            const float px = hx - shoot.vx * 0.02f * k;
            const float py = hy - shoot.vy * 0.02f * k;
            const float fade = (1.0f - f) * (1.0f - progress * 0.3f);
            plotMax(dst, rows, w, static_cast<int>(px), static_cast<int>(py) - y0, clamp8f(220 * fade), clamp8f(225 * fade),
                    clamp8f(255 * fade));
        }
    }
}

} // namespace

extern const BgAnimation bg_anim_starfield;
const BgAnimation bg_anim_starfield = {
    "starfield",
    "Starfield",
    {{"density", "Stars", 45}, {"twinkle", "Twinkle", 50}, {"shooting", "Shooting stars", 30}, {"drift", "Drift", 20}},
    init,
    frame,
    band,
};

#endif // GAGGIMATE_SIM
