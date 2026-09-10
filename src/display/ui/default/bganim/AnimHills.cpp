#ifndef GAGGIMATE_SIM

// "Hills": entry 37 of tools/animbench/web/anim_bench.html, the page the
// owner approved the look on. Three rolling ridges over a dark sky, scrolling
// at 4, 8 and 16 px/s at speed 50, far to near. Each ridge is two drifting
// sine harmonics, so no ridge line repeats. Above every ridge sits a
// quadratic haze band, 22 px on the far layer down to 11 px on the near one,
// and below it the layer fades in over 14, 12 or 10 rows, which is what makes
// the boundary between two layers read as distance. About 90 stars drift
// across the sky at 0.6 px/s under the hills, each at sub-pixel x with a dim
// vertical cross and its own slow twinkle. The sky is a quadratic vertical
// ramp and the layers sit in the 40..120 index band, so mean luma stays
// readable.
//
// Eight parameters, and every one of them is read in frame(): speed, ridge
// relief and layer contrast as they always were, brightness through the
// palette, and four added in gm-3vj.40. Ridge spacing (p[4]) moves the two
// far bases toward the near one. Ridge haze (p[5]) scales every haze band's
// height and the quarter-pixel profile with it. Sky tone (p[6]) lifts the
// palette index the sky starts from. Star density (p[7]) sets how many of one
// fixed star field are drawn. At 50 each of the four is exactly the constant
// this file used to hard-code, so the default picture is unchanged pixel for
// pixel. band(), bandRef() and the two Xtensa
// kernels are untouched: what the four parameters change is the tables and
// the per-layer constants frame() writes.
//
// The page scales the shared theme ramp by its own Brightness parameter,
// themeRamp(176 + round(p[3] * 0.8)), and frame() keeps that: the parameter
// is the page's, not the global animation brightness, and the look the owner
// approved is the scaled one.
//
// Heights keep the page's Q8 scheme, stored as signed offsets from each
// layer's integer base rather than absolute, so they fit int16: the largest
// excursion is (35 + 12) * 1.7 = 79.9 px, 20,455 Q8 units.
//
// What is restructured against the page, and why. The page renders a whole
// frame at a time, so it can build a per-row crossing list once per layer and
// walk it. band() renders 2 rows at a time on the device and every row's
// pixels have to depend only on its absolute y (BgAnim.h's contract), so a
// crossing list would have to be held for all three layers at once: 3 * 480 *
// 40 int16 is 115 kB, and the pool it would come from is 9,216 B. Instead
// frame() stores only the Q8 height per column plus, per 16-column tile, the
// minimum and maximum height in that tile. Everything a row needs then comes
// out of one threshold comparison:
//
// (the thresholds are written as multiplications by 256, not shifts: y can
// sit above a layer's base, so the value shifted would be negative and a
// left shift of a negative int is undefined behaviour, which the fuzzer's
// UBSan build reports)
//
//   covered(x)  <=>  y >= base + (hq[x] >> 8) + 1 + fade  <=>  hq[x] <  covThr
//   in band(x)  <=>  y - (base + (hq[x] >> 8)) in [-gn, fade]
//                                              <=>  covThr <= hq[x] < edgeHi
//
// with covThr = (y - base - fade) << 8 and edgeHi = (y - base + gn + 1) << 8.
// The two ranges are adjacent, which is the page's own design: the fill
// starts one row below the fade band. A tile whose [min, max] sits wholly on
// one side of covThr is filled or skipped without reading a single height, and
// a tile that does not reach covThr or starts past edgeHi needs no edge work
// at all. That is where the cost went: rendering every column of every row in
// the layer's vertical span cost 1.18 ms of the 1.33 ms host frame, and the
// tile test takes it to the columns whose own band actually covers the row.
// The host bench now reports 0.428 ms a frame against plasma's 0.130 in the
// same run, which puts this fourth in the fleet behind ember at 0.743 and
// nebula at 0.627. About 52,700 columns a frame reach the edge pass, against
// the 42,000 a per-column pass would touch, so the 16-column tile grain costs
// a quarter over the ideal and the row scan it replaced cost six times it.
//
// The 40 crossings per row per layer the page caps at are still capped, and
// still the first 40 in increasing x: a tile that is wholly covered or wholly
// uncovered contains no coverage toggle, so skipping it cannot lose one.
// Nothing in the fleet's parameter range comes near the cap. The ridge is two
// harmonics of period 116 px or more over 480 px, so a row is crossed at most
// about thirteen times.
//
// A row that one layer covers from edge to edge hides the sky, the stars and
// every layer drawn before it, because that layer's fill is the full width and
// runs after them. frame() records each layer's global height extremes so
// renderRow can find the last such layer and start there.
//
// Palette indices, Q8 cover weights and the JS truncation points all match the
// page. Blends run in RGB565 channel units with the same floor rounding
// pcMix32 uses; the page blends expanded RGB888 and keeps intermediate low
// bits the panel does not have, so a blended channel can land one RGB565 step
// low. Measured against the page rendered at the bench's three golden frame
// times, with the page output quantized to RGB565 to separate the two
// effects: mean absolute channel-sum difference 0.91 of 765, maximum 27, and
// the whole difference is one step inside the haze and fade bands. Nothing is
// cached across band() calls and no row is copied from another.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>
#include <stdint.h>

// -DGM_BGANIM_HILLS_ASM=0 drops the Xtensa kernels and renders through
// bandRef() verbatim, which is the A/B for the kernels on the device.
#ifndef GM_BGANIM_HILLS_ASM
#define GM_BGANIM_HILLS_ASM 1
#endif

namespace {
using namespace bganim;

constexpr int LAYERS = 3;
constexpr int CAP = 40;                     // the page's crossings per row per layer
constexpr int GLOWN = 22;                   // far layer haze height at Ridge haze 50
constexpr int GLOW_MAX = GLOWN * 2;         // and at Ridge haze 100, which sizes the profile
constexpr int GLOW_SIZE = GLOW_MAX * 4 + 8; // quarter-pixel profile plus zero pad
constexpr int GLOW_PEAK = 185;              // Q8 haze weight where haze meets fade
constexpr int NSTAR = 90;                   // stars at Star density 50
constexpr int NSTAR_MAX = NSTAR * 2;        // and at Star density 100, which sizes the tables
// Ridge spacing holds the near layer where it is and slides the other two
// toward it, so the anchor is the near layer's own base.
constexpr int SPREAD_ANCHOR = 424;
constexpr int TILE = 16;                          // columns per skip tile
constexpr int MAX_W = 480;                        // panel width, the largest render width
constexpr int MAX_TILES = (MAX_W + TILE - 1) / TILE;
constexpr int SS_N = 257;                         // smoothstep entries, 0..256 inclusive
constexpr int CONST_VECS = 7;                     // PIE constant vectors, 8 lanes each
constexpr int WORK_N = 5 * TILE + CONST_VECS * 8; // scratch lanes plus the constant table

// Per layer, as the page's LAY table has it. Periods are pixels per sine
// cycle, rates are extra harmonic phase cycles per ms, and idx is the
// palette index at the ridge line before the contrast parameter.
struct LayerDef {
    int base, a1, a2, period1, period2, speed, index, fade, glow;
    float rate1, rate2;
};

// Returned by value rather than held in a table: three of these would be a
// permanent read-only object in DRAM, and frame() reads each one once.
LayerDef definition(int l) {
    if (l == 0) return {268, 26, 9, 380, 168, 4, 126, 14, GLOWN, 0.000041f, 0.000027f};
    if (l == 1) return {346, 31, 11, 320, 141, 8, 98, 12, 16, 0.000033f, 0.000051f};
    return {424, 35, 12, 265, 116, 16, 64, 10, 11, 0.000059f, 0.000037f};
}

// Row constants for one layer, rebuilt every frame. topFloor and topCeil
// bracket the layer's float top row so rowIndex() can reproduce the page's
// truncation toward zero on either side of it.
struct Layer {
    int base, fade, gn, fadeInv, idx0, topFloor, topCeil, hmin, hmax;
    uint16_t glow;
};

struct StarDef {
    float x;
    uint16_t y, phase;
    uint8_t bright;
};
struct Star {
    uint16_t x, colour;
    uint8_t y, fraction;
};
static_assert(sizeof(Star) == 6, "the hot budget below counts this layout");

int16_t *heightQ = nullptr;    // [LAYERS * w]           Q8 height offsets, read per pixel
int16_t *tileBound = nullptr;  // [LAYERS * MAX_TILES * 2] per-tile {min, max} of heightQ
uint8_t *glowTab = nullptr;    // [GLOW_SIZE]            quarter-pixel haze weight
uint16_t *smooth = nullptr;    // [SS_N]                 smoothstep, 0..256
uint16_t *palette = nullptr;   // [256]                  theme ramp at this brightness
Star *stars = nullptr;         // [NSTAR_MAX]            projected stars
uint8_t *starRow = nullptr;    // [NSTAR_MAX]            star rows alone, for the row scan
Layer *layers = nullptr;       // [LAYERS]
uint16_t *work = nullptr;      // [WORK_N]               tile scratch plus PIE constants
uint16_t *skyCol = nullptr;    // [allocH]               sky colour per row, dither included
StarDef *starDef = nullptr;    // [NSTAR_MAX]            fixed star field, frame() only, PSRAM
const int16_t *sine = nullptr; // borrowed shared sine LUT

int allocW = 0, allocH = 0;
int starTop = 0, starBot = 0; // rows a star can touch, inclusive
int starN = NSTAR;            // stars actually drawn, from the density parameter
int lastBright = -1;
int lastHaze = -1;
int lastSky = -1;
uint32_t lastThemeGen = 0xFFFFFFFFu;

// Hot slab at w = 480, h = 480, each table's own size and then what allocHot
// rounds it to: heightQ 2,880, tileBound 360 (368), glowTab 184 (192), smooth
// 514 (528), palette 512 (512), stars 1,080 (1,088), starRow 180 (192),
// layers 120 (128), work 272 (272), skyCol 960 (960). The star and haze
// tables are sized for the top of their parameter ranges, not for the
// defaults. Measured total 7,120 B of the 9,216 B an animation may take,
// 5,200 B at w = 240 and 5,168 B at w = 233, with no fallback to PSRAM and
// the slab back to empty after release(). PSRAM holds starDef, 2,160 B,
// which frame() reads and band() never does. Every table read per pixel or
// per row comes from allocHot, and nothing is allocated in frame() or
// band().
void release();

// The page's mulberry32, in uint32 arithmetic. JS does the same work on
// int32 with Math.imul and >>> shifts, which is the same bit pattern
// throughout, and divides the result by 2^32 to get its 0..1 fraction.
uint32_t mulberry(uint32_t &seed) {
    seed += 0x6D2B79F5u;
    uint32_t t = (seed ^ (seed >> 15)) * (1u | seed);
    t = (t + (t ^ (t >> 7)) * (61u | t)) ^ t;
    return t ^ (t >> 14);
}

bool init(int w, int h) {
    if (w <= 0 || w > MAX_W || h <= 0 || h > 480) return false;
    if (heightQ != nullptr && w == allocW && h == allocH) return true;
    release();
    allocW = w;
    allocH = h;
    sine = sinLut();
    if (sine == nullptr) {
        release();
        return false;
    }
    heightQ = static_cast<int16_t *>(allocHot(LAYERS * w * sizeof(int16_t)));
    tileBound = static_cast<int16_t *>(allocHot(LAYERS * MAX_TILES * 2 * sizeof(int16_t)));
    glowTab = static_cast<uint8_t *>(allocHot(GLOW_SIZE));
    smooth = static_cast<uint16_t *>(allocHot(SS_N * sizeof(uint16_t)));
    palette = static_cast<uint16_t *>(allocHot(256 * sizeof(uint16_t)));
    stars = static_cast<Star *>(allocHot(NSTAR_MAX * sizeof(Star)));
    starRow = static_cast<uint8_t *>(allocHot(NSTAR_MAX));
    layers = static_cast<Layer *>(allocHot(LAYERS * sizeof(Layer)));
    work = static_cast<uint16_t *>(allocHot(WORK_N * sizeof(uint16_t)));
    skyCol = static_cast<uint16_t *>(allocHot(static_cast<size_t>(h) * sizeof(uint16_t)));
    starDef = static_cast<StarDef *>(alloc(NSTAR_MAX * sizeof(StarDef)));
    // One test over every pointer, and release() on any failure, so a half
    // finished init() leaves the slab exactly as it found it and the retry
    // BgAnim.h promises starts from scratch.
    if (heightQ == nullptr || tileBound == nullptr || glowTab == nullptr || smooth == nullptr || palette == nullptr ||
        stars == nullptr || starRow == nullptr || layers == nullptr || work == nullptr || skyCol == nullptr ||
        starDef == nullptr) {
        release();
        return false;
    }
    for (int k = 0; k < SS_N; k++) {
        const float t = k * (1.0f / 256.0f);
        smooth[k] = static_cast<uint16_t>(256.0f * t * t * (3.0f - 2.0f * t) + 0.5f);
    }
    // PIE constant table, in the order the blend kernel walks it: ones,
    // 256, and then per channel a mask and the constant that puts the
    // channel back in its bit field. There is no vector left shift, so the
    // repositioning is a real multiply by 2048 (red) or 32 (green).
    uint16_t *c = work + 5 * TILE;
    for (int i = 0; i < 8; i++) {
        c[i] = 1;
        c[8 + i] = 256;
        c[16 + i] = 0xF800;
        c[24 + i] = 2048;
        c[32 + i] = 0x07E0;
        c[40 + i] = 32;
        c[48 + i] = 0x001F;
    }
    // Star field: fixed positions, a twinkle phase and a brightness class
    // each, drawn from the page's seed in the page's call order.
    // Drawn in order, so the first starN of them are the same stars whatever
    // the density parameter is, and the row extremes below cover the whole
    // field rather than the drawn part: a wider row scan finds nothing extra,
    // a narrower one would drop a star.
    uint32_t rng = 0x5eed51u;
    starTop = 480;
    starBot = 0;
    for (int i = 0; i < NSTAR_MAX; i++) {
        StarDef &s = starDef[i];
        // Multiply before the conversion so the PRNG fraction is not rounded
        // first; the page holds this in a Float32Array too.
        s.x = static_cast<float>(static_cast<uint64_t>(mulberry(rng)) * static_cast<uint32_t>(w)) *
              (1.0f / 4294967296.0f);
        s.y = static_cast<uint16_t>(8 + ((static_cast<uint64_t>(mulberry(rng)) * 214) >> 32));
        s.phase = static_cast<uint16_t>(mulberry(rng) >> 22);
        s.bright = static_cast<uint8_t>(104 + ((static_cast<uint64_t>(mulberry(rng)) * 46) >> 32));
        if (s.y < starTop) starTop = s.y;
        if (s.y > starBot) starBot = s.y;
    }
    // A star writes its own row and one row either side.
    starTop = starTop > 0 ? starTop - 1 : 0;
    starBot = starBot + 1 < h ? starBot + 1 : h - 1;
    return true;
}

// The page's BY table, 0, 128, 64, 192 in Q8 by y & 3. Every layer here is a
// row fill, so one index step in a slow vertical ramp would land as a hard
// line across the whole panel; interleaving four rows turns each step into a
// soft handover.
BGANIM_INLINE int ditherY(int y) { return ((y & 1) << 7) | ((y & 2) << 5); }

// Haze profile at quarter-pixel steps, over gn rows. Sampled per whole row it
// steps wherever the ridge crosses a row boundary, which shows as vertical
// stripes through the band. The pad past gn * 4 holds zero so a clamped index
// reads a weight of nothing, which is also what lets a layer whose own haze
// is shorter than gn truncate the profile instead of rescaling it, the way
// the page does by running its haze loop from its own height down to 1.
void buildGlow(int gn) {
    for (int k = 0; k < GLOW_SIZE; k++) {
        const float u = k < gn * 4 ? k * (1.0f / (gn * 4)) : 1.0f;
        glowTab[k] = static_cast<uint8_t>(GLOW_PEAK * (1.0f - u) * (1.0f - u) + 0.5f);
    }
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[BG_ANIM_PARAMS]) {
    bool palNew = false;
    if (lastBright != p[3] || lastThemeGen != themeGen()) {
        // The page's own brightness, 176 + round(p[3] * 0.8), on top of the
        // shared theme tone. round(x) is floor(x + 1/2), which for p in
        // 0..100 is exactly (p * 4 + 2) / 5 in integers.
        buildThemeRamp(palette, static_cast<uint16_t>(176 + (static_cast<int>(p[3]) * 4 + 2) / 5));
        lastBright = p[3];
        lastThemeGen = themeGen();
        palNew = true;
    }
    // Ridge haze, p[5]: the height of every haze band as a fraction of the
    // page's, none at 0, the page's own 22, 16 and 11 rows at 50, twice that
    // at 100. The profile is built at the far layer's height, which is the
    // longest, and the two nearer layers truncate it. Rebuilt only when the
    // slider moves, so a frame pays nothing for it.
    if (lastHaze != p[5]) {
        int gnFull = GLOWN * static_cast<int>(p[5]) / 50;
        if (gnFull < 1) gnFull = 1; // the sub-pixel row above a ridge still reads the profile
        if (gnFull > GLOW_MAX) gnFull = GLOW_MAX;
        buildGlow(gnFull);
        lastHaze = p[5];
    }
    // Sky tone, p[6]: how light the night sky is. The page starts the sky at
    // palette index 22 and lifts it 44 more toward the far ridge line; this
    // moves the starting index, black at slider 0, the page's 22 at 50 and 44
    // at 100, and leaves the lift alone. Moving the lift instead was tried
    // and dropped: the sky is a flat row fill dithered over four rows, so a
    // steeper ramp puts a palette step every five or six rows and the sky
    // reads as horizontal bands. Shifting the whole ramp adds no step at all.
    // The row colours go in a table because it is rebuilt only when the
    // slider or the palette moves, never per frame, which also takes a
    // division out of the row path.
    if (palNew || lastSky != p[6]) {
        const int skyBase = 22 * static_cast<int>(p[6]) / 50;
        for (int y = 0; y < allocH; y++) {
            // The numerator is under 2^32 for any height up to 480 and the
            // height is never zero.
            const uint32_t skyQ = 44u * static_cast<uint32_t>(y) * static_cast<uint32_t>(y) * 256u /
                                  (static_cast<uint32_t>(allocH) * static_cast<uint32_t>(allocH));
            int skyIdx = (skyBase * 256 + static_cast<int>(skyQ) + ditherY(y)) >> 8;
            if (skyIdx > 255) skyIdx = 255;
            skyCol[y] = palette[skyIdx];
        }
        lastSky = p[6];
    }
    const float tt = static_cast<float>(tMs) * speedMul(p[0]);
    const float relief = 0.5f + p[1] * 0.012f;
    const float contrast = 0.6f + p[2] * 0.008f;
    // Star density, p[7]: none at 0, the page's 90 at 50, 180 at 100. The
    // stars drawn are always the first starN of one fixed field, so raising
    // the slider adds stars and never moves the ones already there.
    starN = static_cast<int>(p[7]) * 9 / 5;
    if (starN > NSTAR_MAX) starN = NSTAR_MAX;

    const float drift = fmodf(tt * 0.0006f, static_cast<float>(w)); // 0.6 px/s at speed 50
    const uint32_t tw = static_cast<uint32_t>(tt * 0.0009f);        // twinkle phase, ~19 min per turn
    for (int i = 0; i < starN; i++) {
        const StarDef &s = starDef[i];
        const float xf = s.x + drift; // below 2w: s.x < w and drift < w
        const int xi = static_cast<int>(xf);
        Star &out = stars[i];
        out.x = static_cast<uint16_t>(xi >= w ? xi - w : xi);
        out.y = static_cast<uint8_t>(s.y);
        out.fraction = static_cast<uint8_t>((xf - xi) * 256.0f);
        const int idx = s.bright + ((sine[(s.phase + tw) & (SIN_N - 1)] * 9) >> 9);
        out.colour = palette[idx > 255 ? 255 : idx];
        starRow[i] = out.y;
    }

    const int nT = (w + TILE - 1) / TILE;
    for (int l = 0; l < LAYERS; l++) {
        const LayerDef d = definition(l);
        Layer &s = layers[l];
        // Ridge spacing, p[4]: the near layer stays where it is and the two
        // behind it slide toward it, so 0 stacks all three ridges on the near
        // line and 100 lifts the far one to row 112. Integer, so 50 is the
        // page's own 268, 346 and 424 exactly.
        const int base = SPREAD_ANCHOR + (d.base - SPREAD_ANCHOR) * static_cast<int>(p[4]) / 50;
        s.base = base;
        s.fade = d.fade;
        s.gn = d.glow * static_cast<int>(p[5]) / 50;
        s.fadeInv = 256 / d.fade; // 18, 21, 25: the page's truncated Q8 reciprocal
        s.idx0 = static_cast<int>(30.0f + (d.index - 30) * contrast + 0.5f);
        s.glow = palette[s.idx0 + 26 > 255 ? 255 : s.idx0 + 26];
        const float a1 = d.a1 * relief, a2 = d.a2 * relief;
        // The page truncates y - topRow toward zero before scaling it, so
        // both brackets of the float top row are needed, not one rounding.
        const float top = base - (a1 + a2);
        s.topFloor = static_cast<int>(floorf(top));
        s.topCeil = static_cast<int>(ceilf(top));
        const float scroll = tt * d.speed * 0.001f;
        const float q1 = tt * d.rate1 * 1024.0f, q2 = tt * d.rate2 * 1024.0f;
        const float k1 = 1024.0f / d.period1, k2 = 1024.0f / d.period2;
        // The page evaluates ((x + scroll) * k + q) | 0 & 1023 per column in
        // doubles. Here the column term is an integer Q16 accumulator over a
        // phase reduced once per layer, for two reasons. The float form needs
        // a 64-bit widening before the mask, because a long uptime at a high
        // speed setting carries the product past INT32_MAX, and that widening
        // is a soft-float helper call on every one of the 2,880 columns a
        // frame. And the unreduced float itself is the less faithful of the
        // two: at an uptime of an hour the phase is around 1e6, where a
        // float's step is 0.06 table entries, while the reduced form's step
        // is 1e-4. The accumulator's own drift is the Q16 rounding of k over
        // 480 columns, under 0.004 of an entry.
        const float p1 = fmodf(scroll * k1 + q1, static_cast<float>(SIN_N));
        const float p2 = fmodf(scroll * k2 + q2, static_cast<float>(SIN_N));
        uint32_t ph1 = static_cast<uint32_t>(p1 * 65536.0f);
        uint32_t ph2 = static_cast<uint32_t>(p2 * 65536.0f);
        const uint32_t st1 = static_cast<uint32_t>(k1 * 65536.0f + 0.5f);
        const uint32_t st2 = static_cast<uint32_t>(k2 * 65536.0f + 0.5f);
        int16_t *hq = heightQ + l * w;
        int16_t *tb = tileBound + l * MAX_TILES * 2;
        int gmin = 32767, gmax = -32768;
        for (int t = 0; t < nT; t++) {
            const int xs = t * TILE;
            const int xe = xs + TILE < w ? xs + TILE : w;
            int lo = 32767, hi = -32768;
            for (int x = xs; x < xe; x++) {
                const int i1 = static_cast<int>((ph1 >> 16) & (SIN_N - 1));
                const int i2 = static_cast<int>((ph2 >> 16) & (SIN_N - 1));
                ph1 += st1;
                ph2 += st2;
                // The page truncates the weighted sine sum toward zero before
                // halving it, and JS's >> is an arithmetic shift, so a
                // negative sum floors. Both are reproduced here.
                const int v = static_cast<int>(sine[i1] * a1 + sine[i2] * a2) >> 1;
                hq[x] = static_cast<int16_t>(v);
                if (v < lo) lo = v;
                if (v > hi) hi = v;
            }
            tb[2 * t] = static_cast<int16_t>(lo);
            tb[2 * t + 1] = static_cast<int16_t>(hi);
            if (lo < gmin) gmin = lo;
            if (hi > gmax) gmax = hi;
        }
        s.hmin = gmin;
        s.hmax = gmax;
    }
    (void)h;
}

// The layer's palette index for one absolute row. The page computes
// ((idx0 << 8) - (((y - topRow) << 8) / 16) + BY[y & 3]) >> 8, and JS's <<
// truncates y - topRow toward zero first, which is y - ceil(top) at or below
// the top row and y - floor(top) above it. The /16 takes one sixteenth of a
// palette index off per row down the layer.
BGANIM_INLINE int rowIndex(const Layer &l, int y) {
    const int dy = y - (y >= l.topCeil ? l.topCeil : l.topFloor);
    const int idx = (l.idx0 * 256 - dy * 16 + ditherY(y)) >> 8;
    return idx < 8 ? 8 : (idx > 255 ? 255 : idx);
}

#if GM_BGANIM_HILLS_ASM
#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)

// ---------------------------------------------------------------------------
// Xtensa LX7 kernels. Both use the PIE unit (CP3), eight 16-bit lanes per q
// register. band() runs on the SleepAnim task and never in an ISR, so
// FreeRTOS saves the q registers and SAR lazily per task and an ssai hoisted
// above a loop survives an interrupt or a task switch. The compiler never
// allocates q registers, so these blocks use q0 to q7 freely and there is no
// constraint syntax to declare them. Neither kernel writes CPENABLE.
//
// ee.vld.128.ip and ee.vst.128.ip mask the low four address bits silently
// instead of trapping, so every span these kernels touch is 16-byte aligned
// by construction: allocHot() hands back 16-byte aligned tables, the scratch
// sub-buffers sit at multiples of TILE * 2 = 32 B inside one of them, and the
// callers below pay a scalar prefix or fall back to scalar code rather than
// hand either kernel a destination that is not aligned.
// ---------------------------------------------------------------------------

// Constant fill, eight pixels per store. colV holds the colour in all eight
// lanes, so the whole body is one vector store and the loop has no back edge.
// This is the animation's largest single cost: the sky is a full-width fill on
// every row and each layer fills the runs it covers, which is about 575,000
// pixels a frame at 480 x 480.
GM_ANIM_IRAM __attribute__((noinline)) void hillsFill8(uint16_t *dst, const uint16_t *colV, int groups) {
    uint16_t *wr = dst;
    const uint16_t *cv = colV;
    asm volatile("ee.vld.128.ip q0, %[cv], 0\n" // colour in all eight lanes
                 "loopnez %[n], 1f\n"
                 "ee.vst.128.ip q0, %[wr], 16\n" // 8 px, pointer += 16 B
                 "1:\n"
                 : [wr] "+r"(wr), [cv] "+r"(cv)
                 : [n] "r"(groups)
                 : "memory");
}

// dst = blendQ8(dst, src, a) over eight lanes at a time, a in 0..256 per lane.
//
// blendQ8's bg + (((fg - bg) * a) >> 8) is rewritten as
// (fg * a + bg * (256 - a)) >> 8 on each channel's raw magnitude, 0..31 for
// red and blue and 0..63 for green, because the raw form needs only unsigned
// products the vector unit has. The two are the same integer for every a in
// 0..256, not only nearly: bg * 256 + (fg - bg) * a over 256, floored, is
// bg + floor((fg - bg) * a / 256), and an arithmetic right shift is that
// floor. Checked exhaustively over all 64 x 64 x 257 operand triples. The
// largest intermediate is max(fg, bg) * 256 = 16,128, well inside the 16 bits
// ee.vmul.u16 keeps and far below the 32,767 where ee.vadds.s16, the only
// vector add, would saturate.
//
// ee.vmul.u16 only shifts right, product >> SAR with the low 16 bits kept, so
// pulling a channel out of its bit field (>> 11 red, >> 5 green, nothing for
// blue) and putting it back (* 2048, * 32, nothing) both go through it: the
// extraction multiplies by the ones vector at SAR 11 or 5, the repositioning
// multiplies by a real constant at SAR 0.
//
// Register budget is what forces channel-serial order rather than grouping
// the extractions by SAR value: q7 (ones) is pinned for the whole call and
// q0 (fg), q1 (bg), q2 (a), q3 (inv) for the whole group, leaving q4 and q5
// as scratch and q6 as the running OR accumulator, so no finished channel
// ever spills. The cost is more ssai than a SAR-batched schedule, and ssai is
// one cycle. The six per-group constants are re-read from the table each
// group and the pointer is rewound once at the bottom, which is one
// instruction against pinning them in registers the file does not have.
//
// 47 instructions per eight pixels, 5.9 per pixel, against about 20 for the
// scalar blend. GCC 14 wraps the block in the windowed-ABI entry, one
// register copy for the write pointer and retw, and spills nothing:
// xtensa-asm14/AnimHills.S has no stack traffic anywhere in the function.
GM_ANIM_IRAM __attribute__((noinline)) void hillsBlend8(uint16_t *dst, const uint16_t *src, const uint16_t *aQ8,
                                                        const uint16_t *ct, int groups) {
    uint16_t *rd = dst;
    uint16_t *wr = dst;
    const uint16_t *sv = src;
    const uint16_t *av = aQ8;
    const uint16_t *cp = ct;
    // rd and wr are the same address at entry and are separate registers
    // because ee.vld.128.ip and ee.vst.128.ip each post-increment their own
    // pointer, and the group's destination has to be read before the other
    // pointers move and written after. scale565Oct in SleepAnimation.cpp
    // splits its pointers for the same reason.
    asm volatile("ee.vld.128.ip q7, %[cp], 16\n" // ones, pinned for the call
                 "loopnez %[n], 1f\n"
                 "ee.vld.128.ip q2, %[av], 16\n"  // a
                 "ee.vld.128.ip q4, %[cp], 16\n"  // 256
                 "ee.vld.128.ip q0, %[sv], 16\n"  // fg
                 "ee.vsubs.s16 q3, q4, q2\n"      // inv = 256 - a
                 "ee.vld.128.ip q1, %[rd], 16\n"  // bg
                 "ee.vld.128.ip q4, %[cp], 16\n"  // maskR 0xF800
                 "ee.andq q5, q0, q4\n"           // fg red, in place
                 "ee.andq q4, q1, q4\n"           // bg red, in place
                 "ssai 11\n"
                 "ee.vmul.u16 q5, q5, q7\n"       // fg red raw, 0..31
                 "ee.vmul.u16 q4, q4, q7\n"       // bg red raw
                 "ssai 0\n"
                 "ee.vmul.u16 q5, q5, q2\n"       // fg raw * a
                 "ee.vmul.u16 q4, q4, q3\n"       // bg raw * inv
                 "ee.vadds.s16 q5, q5, q4\n"      // sum, at most 7,936
                 "ssai 8\n"
                 "ee.vmul.u16 q5, q5, q7\n"       // out red raw = sum >> 8
                 "ee.vld.128.ip q4, %[cp], 16\n"  // 2048
                 "ssai 0\n"
                 "ee.vmul.u16 q6, q5, q4\n"       // accumulator = red << 11
                 "ee.vld.128.ip q4, %[cp], 16\n"  // maskG 0x07E0
                 "ee.andq q5, q0, q4\n"
                 "ee.andq q4, q1, q4\n"
                 "ssai 5\n"
                 "ee.vmul.u16 q5, q5, q7\n"       // fg green raw, 0..63
                 "ee.vmul.u16 q4, q4, q7\n"
                 "ssai 0\n"
                 "ee.vmul.u16 q5, q5, q2\n"
                 "ee.vmul.u16 q4, q4, q3\n"
                 "ee.vadds.s16 q5, q5, q4\n"      // sum, at most 16,128
                 "ssai 8\n"
                 "ee.vmul.u16 q5, q5, q7\n"
                 "ee.vld.128.ip q4, %[cp], 16\n"  // 32
                 "ssai 0\n"
                 "ee.vmul.u16 q5, q5, q4\n"       // green << 5
                 "ee.orq q6, q6, q5\n"
                 "ee.vld.128.ip q4, %[cp], 16\n"  // maskB 0x001F
                 "ee.andq q5, q0, q4\n"           // blue is already raw
                 "ee.andq q4, q1, q4\n"
                 "ee.vmul.u16 q5, q5, q2\n"       // SAR is still 0 here
                 "ee.vmul.u16 q4, q4, q3\n"
                 "ee.vadds.s16 q5, q5, q4\n"
                 "ssai 8\n"
                 "ee.vmul.u16 q5, q5, q7\n"       // out blue raw
                 "ee.orq q6, q6, q5\n"
                 "ee.vst.128.ip q6, %[wr], 16\n"
                 "addi %[cp], %[cp], -96\n"       // rewind the six per-group vectors
                 "1:\n"
                 : [rd] "+r"(rd), [wr] "+r"(wr), [sv] "+r"(sv), [av] "+r"(av), [cp] "+r"(cp)
                 : [n] "r"(groups)
                 : "memory");
}

#else
// Portable twins of the two kernels above: same names, same signatures, same
// arithmetic, so the dispatching code below is one piece of source whichever
// branch compiled. This branch is what the host bench and the fuzzers run,
// and what GM_BGANIM_NO_ASM selects on the device. Both are copied from
// tools/qemubench/tests/anim_hills/main.c, the plain C references that file
// checks the kernels against under QEMU.
void hillsFill8(uint16_t *dst, const uint16_t *colV, int groups) {
    const uint16_t c = colV[0];
    const int n = groups * 8;
    for (int i = 0; i < n; i++) dst[i] = c;
}
void hillsBlend8(uint16_t *dst, const uint16_t *src, const uint16_t *aQ8, const uint16_t *, int groups) {
    const int n = groups * 8;
    for (int i = 0; i < n; i++) {
        const int a = aQ8[i], inv = 256 - a;
        const int bg = dst[i], fg = src[i];
        const int r = (((fg >> 11) & 0x1F) * a + ((bg >> 11) & 0x1F) * inv) >> 8;
        const int g = (((fg >> 5) & 0x3F) * a + ((bg >> 5) & 0x3F) * inv) >> 8;
        const int b = ((fg & 0x1F) * a + (bg & 0x1F) * inv) >> 8;
        dst[i] = static_cast<uint16_t>((r << 11) | (g << 5) | b);
    }
}
#endif // __XTENSA__ && !GM_BGANIM_NO_ASM
#endif // GM_BGANIM_HILLS_ASM

// Fill n pixels with one colour. The vector path needs a 16-byte aligned
// destination, so it pays a scalar prefix of up to seven pixels to get there
// and a scalar tail for the last group; below 16 pixels the prefix would eat
// the whole run, so short runs stay scalar.
template <bool Asm> BGANIM_INLINE void fillRun(uint16_t *dst, const uint16_t *colV, uint16_t colour, int n) {
#if GM_BGANIM_HILLS_ASM
    if (Asm && n >= 16) {
        const int pre = static_cast<int>((-reinterpret_cast<uintptr_t>(dst)) & 15u) >> 1;
        for (int i = 0; i < pre; i++) dst[i] = colour;
        const int groups = (n - pre) >> 3;
        hillsFill8(dst + pre, colV, groups);
        for (int i = pre + (groups << 3); i < n; i++) dst[i] = colour;
        return;
    }
#else
    (void)colV;
#endif
    for (int i = 0; i < n; i++) dst[i] = colour;
}

// dst = blendQ8(dst, src, a) over n pixels. src and a are scratch buffers and
// are always aligned; dst is a row offset by a whole tile, so it is aligned
// whenever the row itself is, which the panel's 64-byte aligned band buffer
// and a width that is a multiple of eight both give. Anything else takes the
// scalar loop, which is the same integer either way.
template <bool Asm>
BGANIM_INLINE void blendRun(uint16_t *dst, const uint16_t *src, const uint16_t *aQ8, const uint16_t *ct, int n) {
#if GM_BGANIM_HILLS_ASM
    if (Asm && (reinterpret_cast<uintptr_t>(dst) & 15u) == 0) {
        const int groups = n >> 3;
        hillsBlend8(dst, src, aQ8, ct, groups);
        for (int i = groups << 3; i < n; i++) dst[i] = blendQ8(dst[i], src[i], aQ8[i]);
        return;
    }
#else
    (void)ct;
#endif
    for (int i = 0; i < n; i++) dst[i] = blendQ8(dst[i], src[i], aQ8[i]);
}

// Cover weight for a tile of columns that lies wholly above its ridge, where
// the fade has not started and the cover is the haze profile alone. The
// caller has established v > yq for every column, so the quarter-pixel index
// is non-negative, and the band's upper edge is the only test left. Half the
// edge columns of a frame come through here, and they need one blend instead
// of two, because the colour they blend toward is the layer's haze colour and
// nothing else.
GM_ANIM_IRAM void hillsHaze(uint16_t *bQ8, const int16_t *hq, int yq, int vHi, int n) {
    for (int x = 0; x < n; x++) {
        const int v = hq[x];
        // Index below (gn + 1) * 4, so inside GLOW_SIZE. The profile is
        // truncated at the layer's own haze height, not rescaled to it, which
        // is what the page does by running its haze loop from gn down to 1.
        bQ8[x] = static_cast<uint16_t>(v < vHi ? glowTab[(v - yq) >> 6] : 0);
    }
}

// Haze and fade weights for one tile of columns, in Q8.
//
// This stays portable C++ on purpose. The body has two data-dependent
// branches per column, the out-of-band and the haze cases leave after about
// four and ten instructions, and only the fade case runs the full seventeen.
// A branchless form that loopnez could wrap has to compute both table
// indices, clamp both, and select, which is twenty-nine instructions for
// every column and needs more live address registers than the window has.
// The branchy form is the cheaper one here, and the two gathers, glowTab by
// quarter pixel and smooth by the fade fraction, are data-dependent lookups
// no vector instruction can do.
//
// aQ8 is the fade's own weight, from the smoothstep. bQ8 is the cover weight:
// the haze profile above the ridge, and below it GLOW_PEAK carried out to
// full as the fade goes opaque. Out of band both are zero, and a blend by
// zero returns the destination unchanged, so the kernels can run the whole
// tile without a mask.
GM_ANIM_IRAM void hillsWeights(uint16_t *aQ8, uint16_t *bQ8, const int16_t *hq, int yr, int gn, int fade, int inv,
                               int n) {
    const int vLo = (yr - fade) * 256;     // in band from here up
    const int vHi = (yr + gn + 1) * 256;   // and below here
    const int yq = yr * 256;               // the ridge's own height, in Q8
    for (int x = 0; x < n; x++) {
        const int v = hq[x];
        int a = 0, b = 0;
        if (v >= vLo && v < vHi) {
            const int dd = yq - v; // Q8 rows below the ridge, negative above it
            if (dd < 0) {
                // Above the ridge: the haze alone, sampled at quarter pixels
                // off the ridge's own sub-pixel height so it does not stripe
                // where the ridge crosses a row. The index is under
                // (gn + 1) * 4, inside GLOW_SIZE.
                b = glowTab[(-dd) >> 6];
            } else {
                // At and below the ridge the haze and the fade have to meet:
                // the haze peaks at the ridge and the fade starts there, so
                // one cover whose colour walks from the haze to the layer
                // while it goes opaque. Blending the two separately dips in
                // the middle, because both lie over a much darker sky.
                int k = (dd * inv) >> 8;
                if (k > 256) k = 256;
                a = smooth[k];
                b = GLOW_PEAK + (((256 - GLOW_PEAK) * a) >> 8);
            }
        }
        aQ8[x] = static_cast<uint16_t>(a);
        bQ8[x] = static_cast<uint16_t>(b);
    }
}

template <bool Asm> GM_ANIM_IRAM void renderRow(uint16_t *out, int y, int w, int nT) {
    uint16_t *aQ8 = work;
    uint16_t *bQ8 = work + TILE;
    uint16_t *tgt = work + 2 * TILE;
    uint16_t *colV = work + 3 * TILE;
    uint16_t *glowV = work + 4 * TILE;
    const uint16_t *ct = work + 5 * TILE;

    // A layer that covers this row from edge to edge hides the sky, the stars
    // and every layer before it, because its own fill is the full width and
    // runs after them. It needs no edge work either: a column is in the band
    // only at or above covThr, and full coverage means every height is below
    // it.
    int first = 0;
    uint16_t firstCol = 0;
    for (int l = LAYERS - 1; l >= 0; l--) {
        const Layer &s = layers[l];
        if (s.hmax < (y - s.base - s.fade) * 256) {
            first = l + 1;
            firstCol = palette[rowIndex(s, y)];
            break;
        }
    }

    if (first == 0) {
        // Sky: a quadratic ramp that lifts toward the far ridge line, built
        // per row in frame() with the dither already in it.
        const uint16_t sky = skyCol[y];
        for (int i = 0; i < TILE; i++) colV[i] = sky;
        fillRun<Asm>(out, colV, sky, w);

        if (y >= starTop && y <= starBot) {
            for (int i = 0; i < starN; i++) {
                const int dy = y - starRow[i];
                if (dy < -1 || dy > 1) continue;
                const Star &s = stars[i];
                const int a = dy == 0 ? 200 : 58; // the core, then the cross arms
                const int x1 = s.x + 1 == w ? 0 : s.x + 1;
                out[s.x] = blendQ8(out[s.x], s.colour, (a * (256 - s.fraction)) >> 8);
                out[x1] = blendQ8(out[x1], s.colour, (a * s.fraction) >> 8);
            }
        }
    } else {
        for (int i = 0; i < TILE; i++) colV[i] = firstCol;
        fillRun<Asm>(out, colV, firstCol, w);
    }

    for (int l = first; l < LAYERS; l++) {
        const Layer &s = layers[l];
        const int yr = y - s.base;
        const int covThr = (yr - s.fade) * 256;   // covered below this height
        const int edgeHi = (yr + s.gn + 1) * 256; // in band from covThr up to here
        const int yq = yr * 256;                  // above this height the fade has not started
        // Nothing of this layer reaches the row: no coverage and no band.
        if (s.hmin >= edgeHi) continue;
        const uint16_t colour = palette[rowIndex(s, y)];
        const int16_t *hq = heightQ + l * w;
        const int16_t *tb = tileBound + l * MAX_TILES * 2;
        for (int i = 0; i < TILE; i++) {
            colV[i] = colour;
            glowV[i] = s.glow;
        }

        // Runs. covered is the coverage of the column just passed, pos the
        // start of the run it belongs to, and n the number of toggles, capped
        // where the page caps its crossing list.
        bool covered = hq[0] < covThr;
        int pos = 0, n = 0;
        for (int t = 0; t < nT && n < CAP; t++) {
            const int lo = tb[2 * t], hi = tb[2 * t + 1];
            const int xs = t * TILE;
            if (hi < covThr) { // the whole tile is covered
                if (t != 0 && !covered) {
                    pos = xs;
                    covered = true;
                    n++;
                }
            } else if (lo >= covThr) { // none of it is
                if (t != 0 && covered) {
                    fillRun<Asm>(out + pos, colV, colour, xs - pos);
                    pos = xs;
                    covered = false;
                    n++;
                }
            } else { // the ridge crosses this tile, so read its columns
                const int xe = xs + TILE < w ? xs + TILE : w;
                for (int x = t != 0 ? xs : 1; x < xe; x++) {
                    const bool cur = hq[x] < covThr;
                    if (cur != covered) {
                        if (covered) fillRun<Asm>(out + pos, colV, colour, x - pos);
                        pos = x;
                        covered = cur;
                        if (++n >= CAP) break;
                    }
                }
            }
        }
        if (covered) fillRun<Asm>(out + pos, colV, colour, w - pos);

        // The anti-aliased ridge line and the haze band above it, then the
        // fade below it, over whatever is already on the row.
        for (int t = 0; t < nT; t++) {
            const int lo = tb[2 * t], hi = tb[2 * t + 1];
            if (hi < covThr || lo >= edgeHi) continue;
            const int xs = t * TILE;
            const int nn = xs + TILE < w ? TILE : w - xs;
            if (lo > yq) { // wholly above the ridge: haze over one colour
                hillsHaze(bQ8, hq + xs, yq, edgeHi, nn);
                blendRun<Asm>(out + xs, glowV, bQ8, ct, nn);
                continue;
            }
            hillsWeights(aQ8, bQ8, hq + xs, yr, s.gn, s.fade, s.fadeInv, nn);
            for (int i = 0; i < nn; i++) tgt[i] = s.glow;
            blendRun<Asm>(tgt, colV, aQ8, ct, nn);       // haze colour walking to the layer
            blendRun<Asm>(out + xs, tgt, bQ8, ct, nn);   // that cover over the row
        }
    }
}

template <bool Asm> GM_ANIM_IRAM void render(uint16_t *dst, int y0, int rows, int w) {
    const int nT = (w + TILE - 1) / TILE;
    for (int row = 0; row < rows; row++) {
        renderRow<Asm>(dst + static_cast<size_t>(row) * w, y0 + row, w, nT);
    }
}

// The portable reference, this file's spec. The host bench renders it against
// the goldens and SleepAnimation::runAnimTest (/api/debug/animtest) renders it
// against band() on the device, which is the only way the kernels above get
// validated: the host bench cannot assemble them.
GM_ANIM_IRAM void bandRef(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    render<false>(dst, y0, rows, w);
}

GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
#if GM_BGANIM_HILLS_ASM
    render<true>(dst, y0, rows, w);
#else
    render<false>(dst, y0, rows, w);
#endif
}

void release() {
    releaseTable(heightQ, LAYERS * static_cast<size_t>(allocW) * sizeof(int16_t));
    releaseTable(tileBound, LAYERS * MAX_TILES * 2 * sizeof(int16_t));
    releaseTable(glowTab, static_cast<size_t>(GLOW_SIZE));
    releaseTable(smooth, SS_N * sizeof(uint16_t));
    releaseTable(palette, 256 * sizeof(uint16_t));
    releaseTable(stars, NSTAR_MAX * sizeof(Star));
    releaseTable(starRow, static_cast<size_t>(NSTAR_MAX));
    releaseTable(layers, LAYERS * sizeof(Layer));
    releaseTable(work, WORK_N * sizeof(uint16_t));
    releaseTable(skyCol, static_cast<size_t>(allocH) * sizeof(uint16_t));
    releaseTable(starDef, NSTAR_MAX * sizeof(StarDef));
    sine = nullptr;
    allocW = allocH = 0;
    starTop = starBot = 0;
    starN = NSTAR;
    lastBright = -1;
    lastHaze = -1;
    lastSky = -1;
    lastThemeGen = 0xFFFFFFFFu;
}

} // namespace

extern const BgAnimation bg_anim_hills;
const BgAnimation bg_anim_hills = {
    "hills",
    "Hills",
    {{"speed", "Speed", 50},
     {"relief", "Ridge relief", 50},
     {"depth", "Layer contrast", 55},
     {"bright", "Brightness", 60},
     {"spread", "Ridge spacing", 50},
     {"haze", "Ridge haze", 50},
     {"sky", "Sky tone", 50},
     {"stars", "Star density", 50}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
