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
// 128 radial steps x 16 Bayer phases of theme-tinted RGB565 background.
//
// The vignette has to be dithered in COLOUR space, not index space like the
// palette animations: it spans theme positions 0..35 only, so consecutive
// radial steps land on the same RGB565 word and perturbing the index changes
// nothing. Undithered it was the second-worst bander in the fleet -- 44.0% of
// disc pixels on a monotone <=1 LSB staircase at brightness 100, and the
// contour rings are plainly visible as concentric discs on a dark sky.
//
// Phase is the MAJOR axis (vigColor[phase * 128 + idx]) so a row resolves its
// four phases into four base pointers once, and band()'s inner loop is then
// byte-identical to the undithered version -- one load, one clamp, one indexed
// load, one store. Indexing the other way (vigColor[idx * 16 + phase]) needs an
// extra shift and add per pixel and measured +32% on band().
//
// The usual objection to phase-major -- consecutive pixels jumping 256 B
// between blocks -- is a PSRAM problem, and this table is 4 KB, under
// SRAM_ALLOC_LIMIT, so it is directly addressable with no cache line to miss.
// If it ever grows past that limit, revisit: in PSRAM this layout is exactly
// the aurora failure mode described in BgAnimCommon.cpp's alloc().
constexpr int VIG_PHASES = 16;
uint16_t *vigColor = nullptr;
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

// Hot-slab placement (BgAnimCommon.h's GM_BGANIM_HOT_SLAB): 9,216 B budget.
// Round 2 ranked by reads/frame and left dx2 in PSRAM on the theory that its
// sequential per-row sweep would stream at close to SRAM speed; production
// proved that wrong (asm regressed from 12.7 ms with everything pinned SRAM
// to 15.8 ms, ref went from faster-than-HEAD to tied with it, i.e. the
// kernel's edge over the portable path vanished along with the placement
// win) -- real PSRAM traffic from the panel's own DMA scanout and the other
// core's LVGL contends for the same cache lines a synthetic sweep-only test
// never has to share, so "small and sequential" was not sufficient evidence
// on its own. This pass ranks by reads PER BYTE (what the fixed 9,216 B
// budget actually buys) among every table band()/bandRef() touch, everyone
// re-measured, nobody exempted by access-pattern theory a second time:
//
//   table      bytes  reads/frame  reads/byte
//   dx2        1,920    230,400      120     once/pixel (sequential, but
//                                              see above: no longer trusted
//                                              to survive real PSRAM
//                                              contention rent-free)
//   vigColor   4,096    230,400       56.3   once/pixel, gather (no
//                                              sequential run at all)
//   starY        800      9,600       12     star-bucket walk (see below)
//   bandNext     800      9,600       12     same walk -- it IS the list
//   bandHead      64        720       11.3   macro-bands touched per call
//   draws      2,400      9,600        4     same walk, LOWEST density
//   dy2        1,920        480        0.25  once/row, not once/pixel
//
// dx2 and vigColor are a matched pair for the same loop -- every pixel
// touches both, so either one left cold reintroduces a per-pixel PSRAM
// stall regardless of the other -- and together they are the two highest-
// density tables besides, so both are non-negotiable: 6,016 B, allocated
// first (dx2 first by the numbers above) so neither can lose the slab to a
// lower-density table if a future edit changes sizes. starY/bandNext/
// bandHead (1,664 B) fit in what is left (3,200 B) with room to spare.
// draws (2,400 B) does not fit in the 1,536 B left after that and is the
// least dense of the four star-bucket tables by a wide margin (4 reads/byte
// against 11-12 for its three neighbors), so it is the one demoted to
// PSRAM: total 7,680 B, fits the 9,216 B budget with 1,536 B spare. dy2
// (once/row, not once/pixel), stars (~400 reads/frame, the largest table in
// the file at 12,800 B), driftQ, starCol (frame()-only) and vigLUT
// (theme-rebuild only) are all far colder and stay in PSRAM; nothing here
// needed shrinking to fit.
bool init(int w, int h) {
    if (stars == nullptr) {
        dx2 = static_cast<int32_t *>(allocHot(w * sizeof(int32_t)));
        vigColor = static_cast<uint16_t *>(allocHot(128 * VIG_PHASES * sizeof(uint16_t)));
        starY = static_cast<int16_t *>(allocHot(MAX_STARS * sizeof(int16_t)));
        bandNext = static_cast<int16_t *>(allocHot(MAX_STARS * sizeof(int16_t)));
        bandHead = static_cast<int16_t *>(allocHot(NUM_BANDS * sizeof(int16_t)));
        stars = static_cast<Star *>(alloc(MAX_STARS * sizeof(Star)));
        draws = static_cast<StarDraw *>(alloc(MAX_STARS * sizeof(StarDraw)));
        driftQ = static_cast<int32_t *>(alloc(MAX_STARS * sizeof(int32_t)));
        allocW = w;
        allocH = h;
        dy2 = static_cast<int32_t *>(alloc(h * sizeof(int32_t)));
        vigLUT = static_cast<uint8_t *>(alloc(128));
        starCol = static_cast<uint8_t *>(alloc(MAX_STARS * 3));
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
    // One RGB565 step is 8.226 of 0..255 in red and blue, 4.048 in green. A
    // 0.75-step peak swing clears the contours (44.0% -> 0.0%) while moving
    // only ~0.2% of pixels by more than a single LSB, so the ordered pattern
    // stays below the noise floor of a 0.13 mm pixel pitch.
    for (int idx = 0; idx < 128; idx++) {
        uint8_t c[3];
        themeRGB((vigLUT[idx] * 36) >> 8, c);
        for (int ph = 0; ph < VIG_PHASES; ph++) {
            const float d = (static_cast<float>(BAYER4[ph]) - 7.5f) * (0.75f / 7.5f);
            vigColor[ph * 128 + idx] = rgb565(clamp8f(c[0] + d * 8.226f), clamp8f(c[1] + d * 4.048f),
                                              clamp8f(c[2] + d * 8.226f));
        }
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

// ---------------------------------------------------------------------
// band() split in two, by cost, not by look: the vignette gather below
// touches every one of the 230,400 pixels in a frame; the star and
// shooting-star plotting after it touches at most a few hundred. Xtensa
// asm effort (see starfieldVigRowAsm below) goes at the vignette gather
// only, it is a per-pixel indexed load with no closed form, so PIE cannot
// vectorise it (no vector gather on this chip; ASM_BRIEF.md), and it is
// where the frame's time actually goes.
//
// plotStarsAndShoot() and vigRowScalar() are the exact per-pixel math
// band() used before this pass, merely pulled out of the old single
// function body, bandRef() below calls them in the same order the old
// band() ran them, so it is pixel-identical to the pre-asm code. band()
// (further below) calls plotStarsAndShoot() too, and only replaces the
// vignette loop, so the two functions can never disagree about star or
// shooting-star pixels, only the asm equivalence test at
// /api/debug/animtest has to prove the vignette gather.
// ---------------------------------------------------------------------

// Piece 2: stars bucketed for this band (plus neighbors for the 1px
// spill), then the shooting star trail. Unmodified extraction of the old
// band()'s second and third sections.
void plotStarsAndShoot(uint16_t *dst, int y0, int rows, int w) {
    int bandLo = (y0 >> 4) - 1, bandHi = ((y0 + rows - 1) >> 4) + 1;
    if (bandLo < 0) {
        bandLo = 0;
    }
    if (bandHi > NUM_BANDS - 1) {
        bandHi = NUM_BANDS - 1;
    }
    for (int b = bandLo; b <= bandHi; b++) {
        for (int16_t i = bandHead[b]; i >= 0; i = bandNext[i]) {
            const int y = starY[i] - y0;
            // starY/bandNext are hot-slab (SRAM); draws[] is PSRAM. A macroband
            // spans 16 rows but this call only covers `rows` (2) of them, so
            // most stars found by the bucket walk cannot land a pixel here: check
            // the row with the cheap SRAM read before paying for the PSRAM one.
            // Pad is +0/-1, not symmetric: the sizeClass>=2 glow plots x,y+1, so
            // a star at y=-1 can still light row 0, but nothing ever reaches
            // back past that or forward past y=rows-1 (all offsets below are
            // x-only except that one +1 row). Measured: ~94% of visited nodes
            // fail this check (30 macrobands averaging ~7 stars each, 3 examined
            // per call, only a 3-row slice of the 48 rows spanned is reachable),
            // so this removes a PSRAM read and up to four plotMax() calls for
            // nearly all of them, at the cost of one branch that was going to
            // be paid inside plotMax() anyway.
            if (y < -1 || y >= rows) {
                continue;
            }
            const StarDraw &d = draws[i];
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
    // shooting star trail (14 segments, band-clipped by plotMax)
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

// Piece 1, portable: one row of the vignette gather (squared-distance LUT,
// no sqrt). Used directly by bandRef() (the spec) and as band()'s fallback
// for any w that is not a multiple of 4, never true in production (w is
// always 480 or 240, both multiples of 16) but kept so the contract holds
// for an arbitrary w, exactly as the old unrestricted loop did.
void vigRowScalar(uint16_t *row, int y, int w) {
    const int32_t dyv = dy2[y];
    // One base pointer per x&3 phase for this row, hoisted out of the loop:
    // inside the unrolled body vp[k] is a register, so the indexed load is
    // the same instruction the undithered version used.
    // Four named pointers, not an array: an array indexed by the unroll
    // counter can spill to the stack and cost a load per pixel, which
    // measured +15% on band() where these cost nothing.
    const uint16_t *const vb = vigColor + (y & 3) * 512;
    const uint16_t *const p0 = vb;
    const uint16_t *const p1 = vb + 128;
    const uint16_t *const p2 = vb + 256;
    const uint16_t *const p3 = vb + 384;
    int x = 0;
    for (; x + 3 < w; x += 4) {
        int i0 = (dyv + dx2[x + 0]) >> 10; // 480x480: max r2 ~115200 -> 112
        int i1 = (dyv + dx2[x + 1]) >> 10;
        int i2 = (dyv + dx2[x + 2]) >> 10;
        int i3 = (dyv + dx2[x + 3]) >> 10;
        if (i0 > 127) {
            i0 = 127;
        }
        if (i1 > 127) {
            i1 = 127;
        }
        if (i2 > 127) {
            i2 = 127;
        }
        if (i3 > 127) {
            i3 = 127;
        }
        row[x + 0] = p0[i0];
        row[x + 1] = p1[i1];
        row[x + 2] = p2[i2];
        row[x + 3] = p3[i3];
    }
    for (; x < w; x++) { // widths not a multiple of 4
        int idx = (dyv + dx2[x]) >> 10;
        if (idx > 127) {
            idx = 127;
        }
        row[x] = vb[(x & 3) * 128 + idx];
    }
}

// The spec. Host bench goldens run against this, and the device equivalence
// test (SleepAnimation::runAnimTest, /api/debug/animtest) checks band()'s
// asm kernel against it pixel for pixel. Pixel-identical to this file's
// band() before this pass: same two pieces, same order, just factored out
// above so band() can reuse plotStarsAndShoot() without duplicating it.
void bandRef(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int ry = 0; ry < rows; ry++) {
        vigRowScalar(dst + static_cast<size_t>(ry) * w, y0 + ry, w);
    }
    plotStarsAndShoot(dst, y0, rows, w);
}

#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
// Vignette gather, four pixels per iteration, hand-written Xtensa scalar.
// PIE has no vector gather instruction on this chip (ASM_BRIEF.md), so this
// stays scalar; the win over vigRowScalar is replacing its four
// `if (i > 127) i = 127;` branches with MIN (Miscellaneous Operations
// option, present on this core), a compare-and-select instead of a
// taken/not-taken branch, and packing each pixel pair into one 32-bit
// store (dst rows are 4-byte aligned; w is always a multiple of 4 in
// production, band() falls back to vigRowScalar() otherwise).
//
// Uses the hardware zero-overhead LOOP (LOOPNEZ) rather than the manual
// `addi`/`bnez` this file's PIE-kernel siblings (SleepAnimation.cpp's
// scale565Oct, AnimNebula.cpp's lerpRowPie) use for their own loops: those
// run at most tens of iterations per call, where a taken branch each pass
// is noise, but this loop runs w/4 times (120 or 60) per row, 480 rows a
// frame, so paying zero cycles for the back edge instead of a taken-branch
// penalty every iteration is worth the one extra setup instruction.
//
// idx0..idx3 never need a signed/unsigned check before the >>10: dyv =
// dy2[y] and dx2[x] are both squared distances, so the sum is always >= 0
// and SRAI (arithmetic) agrees with SRLI (logical) here; SRAI is used
// because it is the shift int32_t dyv + dx2[x] uses in the C++ reference.
//
// A load's destination register doubles as its own base-address register
// in the four L16UI below (e.g. `l16ui t1, t1, 0`): the effective address
// is read from the base value before the load overwrites it, so this is
// architecturally safe, and it is one of the moves this loop cannot spare
// a register to avoid, see the budget note below. Confirmed assembling
// and producing correct results in xtensa-asm14 and the QEMU test
// (tools/qemubench/tests/anim_starfield/).
//
// Register budget: dxp, rowp, dyv, p0..p3, n (8, live for the whole loop)
// + c127, t1..t4 (5, c127 set once before the loop, t1..t4 reused in place
// for index then value) = 13, at the ~13-usable-AR ceiling ASM_BRIEF.md
// documents for inline asm inside a windowed-ABI function. Checked in
// xtensa-asm14/AnimStarfield.S: no spill around this block (report in this
// pass's final message).
__attribute__((noinline)) static void starfieldVigRowAsm(uint16_t *__restrict row, const int32_t *__restrict dx2Row,
                                                          int32_t dyv, const uint16_t *__restrict p0,
                                                          const uint16_t *__restrict p1, const uint16_t *__restrict p2,
                                                          const uint16_t *__restrict p3, int n4) {
    const int32_t *dxp = dx2Row;
    uint16_t *rowp = row;
    int32_t t1, t2, t3, t4, c127; // scratch; values unused after the block
    asm volatile("movi %[c127], 127\n"
                 "loopnez %[n], 2f\n"
                 "l32i    %[t1], %[dxp], 0\n"  // dx2[x+0]
                 "l32i    %[t2], %[dxp], 4\n"  // dx2[x+1]
                 "add     %[t1], %[t1], %[dyv]\n"
                 "add     %[t2], %[t2], %[dyv]\n"
                 "l32i    %[t3], %[dxp], 8\n"  // dx2[x+2]
                 "l32i    %[t4], %[dxp], 12\n" // dx2[x+3]
                 "srai    %[t1], %[t1], 10\n"
                 "srai    %[t2], %[t2], 10\n"
                 "add     %[t3], %[t3], %[dyv]\n"
                 "add     %[t4], %[t4], %[dyv]\n"
                 "min     %[t1], %[t1], %[c127]\n"
                 "min     %[t2], %[t2], %[c127]\n"
                 "srai    %[t3], %[t3], 10\n"
                 "srai    %[t4], %[t4], 10\n"
                 "min     %[t3], %[t3], %[c127]\n"
                 "min     %[t4], %[t4], %[c127]\n"
                 "addx2   %[t1], %[t1], %[p0]\n" // t1 = &p0[idx0]
                 "addx2   %[t2], %[t2], %[p1]\n" // t2 = &p1[idx1]
                 "l16ui   %[t1], %[t1], 0\n"     // t1 = p0[idx0]
                 "l16ui   %[t2], %[t2], 0\n"     // t2 = p1[idx1]
                 "addx2   %[t3], %[t3], %[p2]\n" // t3 = &p2[idx2]
                 "addx2   %[t4], %[t4], %[p3]\n" // t4 = &p3[idx3]
                 "l16ui   %[t3], %[t3], 0\n"     // t3 = p2[idx2]
                 "l16ui   %[t4], %[t4], 0\n"     // t4 = p3[idx3]
                 "slli    %[t2], %[t2], 16\n"
                 "slli    %[t4], %[t4], 16\n"
                 "or      %[t1], %[t1], %[t2]\n" // pixels x+0,x+1 packed
                 "or      %[t3], %[t3], %[t4]\n" // pixels x+2,x+3 packed
                 "s32i    %[t1], %[rowp], 0\n"
                 "s32i    %[t3], %[rowp], 4\n"
                 "addi    %[dxp], %[dxp], 16\n" // four int32_t
                 "addi    %[rowp], %[rowp], 8\n" // four uint16_t
                 "2:\n"
                 : [dxp] "+r"(dxp), [rowp] "+r"(rowp), [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3),
                   [t4] "=&r"(t4), [c127] "=&r"(c127)
                 : [dyv] "r"(dyv), [p0] "r"(p0), [p1] "r"(p1), [p2] "r"(p2), [p3] "r"(p3), [n] "r"(n4)
                 : "memory");
}
#endif

void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
    for (int ry = 0; ry < rows; ry++) {
        const int y = y0 + ry;
        uint16_t *row = dst + static_cast<size_t>(ry) * w;
        if ((w & 3) == 0) {
            const int32_t dyv = dy2[y];
            const uint16_t *const vb = vigColor + (y & 3) * 512;
            starfieldVigRowAsm(row, dx2, dyv, vb, vb + 128, vb + 256, vb + 384, w >> 2);
        } else { // never hit in production: w is always 480 or 240
            vigRowScalar(row, y, w);
        }
    }
    plotStarsAndShoot(dst, y0, rows, w);
#else
    bandRef(dst, y0, rows, w, tMs, p);
#endif
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
    releaseTable(vigColor, 128 * VIG_PHASES * sizeof(uint16_t));
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
    bandRef,
};

#endif // GAGGIMATE_SIM
