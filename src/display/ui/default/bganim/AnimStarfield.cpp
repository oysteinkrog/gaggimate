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
int32_t *driftQ = nullptr;      // per-star drift phase, Q16.16 px, integer-wrapped mod w
int32_t *dx2 = nullptr, *dy2 = nullptr;
uint8_t *vigLUT = nullptr;    // 128 entries
uint8_t *starCol = nullptr;   // MAX_STARS * 3, per-star base color from the theme
uint16_t *vigColor = nullptr; // 128 RGB565 background entries (theme-tinted)
uint8_t shootCol[3] = {220, 225, 255};
uint32_t lastThemeGen = 0xFFFFFFFF;
int g_nStars = 0;

void rebuildThemeAssets();

struct Shoot {
    bool active = false;
    float x, y, vx, vy, life, maxLife;
    float invMaxLife; // 1/maxLife, precomputed once at trigger time so band()'s
                       // per-band progress calc is a multiply, not a divide.
};
Shoot shoot;
uint32_t nextShootMs = 6000;
uint32_t rng = 0xC0FFEE;
uint32_t lastTMs = 0;
uint32_t lastDriftMs = 0xFFFFFFFF; // sentinel: no drift step on the very first frame() call
int allocW = 0, allocH = 0;        // dimensions dx2/dy2 were sized for

bool init(int w, int h) {
    if (stars == nullptr) {
        stars = static_cast<Star *>(alloc(MAX_STARS * sizeof(Star)));
        draws = static_cast<StarDraw *>(alloc(MAX_STARS * sizeof(StarDraw)));
        starY = static_cast<int16_t *>(alloc(MAX_STARS * sizeof(int16_t)));
        bandHead = static_cast<int16_t *>(alloc(NUM_BANDS * sizeof(int16_t)));
        bandNext = static_cast<int16_t *>(alloc(MAX_STARS * sizeof(int16_t)));
        driftQ = static_cast<int32_t *>(alloc(MAX_STARS * sizeof(int32_t)));
        allocW = w;
        allocH = h;
        dx2 = static_cast<int32_t *>(alloc(w * sizeof(int32_t)));
        dy2 = static_cast<int32_t *>(alloc(h * sizeof(int32_t)));
        vigLUT = static_cast<uint8_t *>(alloc(128));
        starCol = static_cast<uint8_t *>(alloc(MAX_STARS * 3));
        vigColor = static_cast<uint16_t *>(alloc(128 * sizeof(uint16_t)));
        if (stars == nullptr || draws == nullptr || starY == nullptr || bandHead == nullptr || bandNext == nullptr ||
            driftQ == nullptr || dx2 == nullptr || dy2 == nullptr || vigLUT == nullptr || starCol == nullptr ||
            vigColor == nullptr) {
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
            // Q16.16 fixed-point drift phase, seeded from the initial float position
            // so frame 0 (dt=0 below) reproduces the old closed-form x exactly.
            driftQ[i] = static_cast<int32_t>(stars[i].x * 65536.0f);
        }
        rebuildThemeAssets();
        lastThemeGen = themeGen();
    }
    return true;
}

// Stars sample the theme's bright end (per-star hue picks the exact spot);
// the vignette background sits in the theme's darkest ~14%.
void rebuildThemeAssets() {
    for (int i = 0; i < MAX_STARS; i++) {
        themeRGB(180 + static_cast<int>(stars[i].hue * 75.0f), &starCol[i * 3]);
    }
    for (int idx = 0; idx < 128; idx++) {
        uint8_t c[3];
        themeRGB((vigLUT[idx] * 36) >> 8, c);
        vigColor[idx] = rgb565(c[0], c[1], c[2]);
    }
    themeRGB(255, shootCol);
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[4]) {
    (void)h;
    if (themeGen() != lastThemeGen) {
        rebuildThemeAssets();
        lastThemeGen = themeGen();
    }
    g_nStars = 40 + (p[1] * (MAX_STARS - 40)) / 100;
    const float t = tMs * 0.001f;
    const float twinkleAmt = p[2] * (1.0f / 100.0f); // reciprocal multiply: dividend isn't a compile-time
                                                      // constant, so the compiler can't fold /100.0f itself
    // Old formula was `scale = 1 - twinkleAmt*(1-twinkle)`, i.e. a depth
    // coefficient equal to twinkleAmt itself (identity). twinkle in [0.4,1.0],
    // so at p[2]=100 (twinkleAmt=1) the dip only ever reached scale=0.4 -- not
    // very dramatic. Replace the identity with a quadratic depth curve solved so
    // depth(0.5)=0.5 (bit-identical to the old default at p[2]=50) but
    // depth(1.0)=5/3, which drives the trough all the way to scale=0 (full
    // extinguish) at p[2]=100: 1 - (5/3)*(1-0.4) = 0. Expands only the top of
    // the range; p[2]<=50 is unchanged from before.
    const float twinkleDepth = twinkleAmt * (twinkleAmt * (4.0f / 3.0f) + (1.0f / 3.0f));
    const float driftPxPerSec = 3.0f * speedMul(p[0]);

    // Integer phase-wrap drift: replaces the old fmodf(absolute_position, w) with
    // a per-star Q16.16 accumulator stepped by real elapsed time (dt) and wrapped
    // with a single compare+subtract (no libm, no divide). dt is clamped so a long
    // pause between frame() calls can't overflow the Q16.16 delta; the very first
    // call (lastDriftMs sentinel) takes dt=0 so star positions start exactly at
    // their init()-seeded x, matching the old t=0 closed form exactly.
    const float rawDt = (tMs - lastDriftMs) * 0.001f;
    const float driftDt = lastDriftMs == 0xFFFFFFFF ? 0.0f : (rawDt < 2.0f ? rawDt : 2.0f);
    lastDriftMs = tMs;
    const int32_t wQ = w << 16;

    // fastSinRad()/fastCosRad() each pay a call8 + load into cosTableF() per
    // invocation (xtensa-asm confirms); hoist the table pointer once per frame
    // and inline the identical lookup math (fastSinRad(r) == cosTab[(r -
    // 1.5707963f)*scale) & 255]) so the per-star loop below issues zero calls
    // for the twinkle oscillator instead of one call8 per star.
    const float *cosTab = cosTableF();
    constexpr float RAD_TO_TAB = 256.0f / 6.2831853f;

    for (int b = 0; b < NUM_BANDS; b++) {
        bandHead[b] = -1;
    }
    for (int i = 0; i < g_nStars; i++) {
        const Star &s = stars[i];
        const float v = driftPxPerSec * s.driftSpeed;
        int32_t pos = driftQ[i] + static_cast<int32_t>(v * driftDt * 65536.0f);
        if (pos >= wQ) {
            pos -= wQ;
        } else if (pos < 0) {
            pos += wQ;
        }
        driftQ[i] = pos;
        const float x = pos * (1.0f / 65536.0f);
        // fminf/fmaxf compile to libcalls on this toolchain (no FPU min/max) --
        // ternaries instead. /20.0f is a compile-time-constant divide, which
        // GCC does NOT fold to a reciprocal multiply under strict IEEE (-O2,
        // no -ffast-math) since the rounding differs -- so do it by hand.
        const float edgeRaw = (x < (w - x) ? x : (w - x)) * 0.05f; // *0.05f == /20.0f
        const float edgeFade = edgeRaw < 1.0f ? edgeRaw : 1.0f;
        const float twinkleRad = t * s.rate + s.phase - 1.5707963f;
        const float twinkle = 0.7f + 0.3f * cosTab[static_cast<int>(twinkleRad * RAD_TO_TAB) & 255];
        float bF = s.baseBrightness * (1.0f - twinkleDepth * (1.0f - twinkle)) * edgeFade;
        bF = bF < 0.0f ? 0.0f : (bF > 1.0f ? 1.0f : bF);
        draws[i].x = static_cast<int16_t>(x);
        draws[i].r = clamp8f(starCol[i * 3 + 0] * bF);
        draws[i].g = clamp8f(starCol[i * 3 + 1] * bF);
        draws[i].b = clamp8f(starCol[i * 3 + 2] * bF);
        draws[i].sizeClass = s.sizeClass;
        const int bandIdx = starY[i] >> 4;
        bandNext[i] = bandHead[bandIdx];
        bandHead[bandIdx] = static_cast<int16_t>(i);
    }

    // Shooting star lifecycle (dt from the frame delta; robust to pauses).
    const float dt = lastTMs != 0 && tMs > lastTMs ? (tMs - lastTMs) * 0.001f : 0.033f;
    lastTMs = tMs;
    const uint8_t shootFreq = p[3];
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
        shoot.invMaxLife = 1.0f / shoot.maxLife; // one divide per shoot trigger (rare), not per band()
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
            row[x] = vigColor[idx];
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
        // Both divides here used to run per band() call (up to 30x/frame while
        // active, x15 for the loop below = 450 __divsf3 libcalls/frame).
        // progress now uses invMaxLife (precomputed once at trigger time in
        // frame()); f uses a compile-time reciprocal constant.
        constexpr float INV14 = 1.0f / 14.0f;
        const float progress = shoot.life * shoot.invMaxLife;
        const float hx = shoot.x + shoot.vx * shoot.life;
        const float hy = shoot.y + shoot.vy * shoot.life;
        for (int k = 0; k < 14; k++) {
            const float f = k * INV14;
            const float px = hx - shoot.vx * 0.02f * k;
            const float py = hy - shoot.vy * 0.02f * k;
            const float fade = (1.0f - f) * (1.0f - progress * 0.3f);
            plotMax(dst, rows, w, static_cast<int>(px), static_cast<int>(py) - y0, clamp8f(shootCol[0] * fade),
                    clamp8f(shootCol[1] * fade), clamp8f(shootCol[2] * fade));
        }
    }
}

void release() {
    releaseTable(stars, static_cast<size_t>(MAX_STARS) * sizeof(Star));
    releaseTable(draws, static_cast<size_t>(MAX_STARS) * sizeof(StarDraw));
    releaseTable(starY, static_cast<size_t>(MAX_STARS) * sizeof(int16_t));
    releaseTable(bandHead, static_cast<size_t>(NUM_BANDS) * sizeof(int16_t));
    releaseTable(bandNext, static_cast<size_t>(MAX_STARS) * sizeof(int16_t));
    releaseTable(driftQ, static_cast<size_t>(MAX_STARS) * sizeof(int32_t));
    releaseTable(dx2, static_cast<size_t>(allocW) * sizeof(int32_t));
    releaseTable(dy2, static_cast<size_t>(allocH) * sizeof(int32_t));
    releaseTable(vigLUT, 128);
    releaseTable(starCol, static_cast<size_t>(MAX_STARS) * 3);
    releaseTable(vigColor, 128 * sizeof(uint16_t));
    allocW = allocH = 0;
    lastThemeGen = 0xFFFFFFFF;
    lastTMs = 0;
    lastDriftMs = 0xFFFFFFFF;
}

} // namespace

extern const BgAnimation bg_anim_starfield;
const BgAnimation bg_anim_starfield = {
    "starfield",
    "Starfield",
    {{"speed", "Drift speed", 50}, {"density", "Stars", 45}, {"twinkle", "Twinkle", 50}, {"shooting", "Shooting stars", 30}},
    init,
    frame,
    band,
    release,
};

#endif // GAGGIMATE_SIM
