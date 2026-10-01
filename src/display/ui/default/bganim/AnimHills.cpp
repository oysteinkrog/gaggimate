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
// pixel.
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
// About 52,700 columns a frame reach the edge pass, against
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
// The row is built in RGB888 and packed to RGB565 once, at the end (gm-pciz,
// 2026-09-12). The page blends expanded 8-bit channels with pcMix32,
// a + (((b - a) * t) >> 8), and chains those blends: haze over sky, the fade
// cover over that, a star's cross over the sky, the next layer's haze over
// the last layer's fill. An intermediate keeps low bits the panel does not
// have, and the earlier version, which blended in RGB565 channel units,
// landed one RGB565 step low on 13,543 to 13,877 of the 230,400 pixels of
// each golden frame, all of them inside the haze and fade bands and under
// the stars. So renderRow now holds one row as three planes of 16-bit
// lanes carrying 8-bit channel values, fills and blends those with the
// page's arithmetic, and packs (r >> 3, g >> 2, b >> 3) into the output at
// the end of the row, which is the quantisation page_vs_golden.js applies
// to the page. Palette colours expand as the page's q565r/q565g do,
// (v & 0xF8) | (v >> 5) and (v & 0xFC) | (v >> 6), so a fill and a blend
// toward a palette colour start from the same bytes on both sides. The
// heights, the row colours, the stars and the profile tables follow the
// page's double arithmetic where a float would round the other way at a
// tie (frame() says where). Against the page rendered at the bench's three
// golden frame times and quantised to RGB565: zero differing pixels.
//
// What the planes cost: three 16-byte stores per eight pixels of fill
// instead of one, and one pack pass over every output row. The planes are
// 2,880 B of the hot slab at w = 480, paid for by dropping the per-row sky
// colour table (the index is a division per row now) and the star row
// table (the row scan reads the star records). Nothing is cached across
// band() calls and no row is copied from another.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

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
// Scratch lanes, in 16-byte units so every sub-buffer is vector aligned:
// the fade weight and the cover weight for one tile, the three planes of
// the fade's target colour, the three planes of the layer colour and of
// the haze colour broadcast over a tile, and the pack kernel's three
// constant vectors (ones, 2048, 32).
constexpr int WK_A = 0;
constexpr int WK_B = TILE;
constexpr int WK_TGT = 2 * TILE;
constexpr int WK_COL = 5 * TILE;
constexpr int WK_GLOW = 8 * TILE;
constexpr int WK_CT = 11 * TILE;
constexpr int WORK_N = WK_CT + 3 * 8;

// Per layer, as the page's LAY table has it. Periods are pixels per sine
// cycle, rates are extra harmonic phase cycles per ms (the page's doubles),
// and idx is the palette index at the ridge line before the contrast
// parameter.
struct LayerDef {
    int base, a1, a2, period1, period2, speed, index, fade, glow;
    double rate1, rate2;
};

// Returned by value rather than held in a table: three of these would be a
// permanent read-only object in DRAM, and frame() reads each one once.
LayerDef definition(int l) {
    if (l == 0) return {268, 26, 9, 380, 168, 4, 126, 14, GLOWN, 0.000041, 0.000027};
    if (l == 1) return {346, 31, 11, 320, 141, 8, 98, 12, 16, 0.000033, 0.000051};
    return {424, 35, 12, 265, 116, 16, 64, 10, 11, 0.000059, 0.000037};
}

// Row constants for one layer, rebuilt every frame. topFloor and topCeil
// bracket the layer's top row so rowIndex() can reproduce the page's
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
Layer *layers = nullptr;       // [LAYERS]
uint16_t *work = nullptr;      // [WORK_N]               tile scratch plus PIE constants
uint16_t *planes = nullptr;    // [3 * planeStride]      one row as r, g, b lanes, 0..255 each
StarDef *starDef = nullptr;    // [NSTAR_MAX]            fixed star field, frame() only, PSRAM
const int16_t *sine = nullptr; // borrowed shared sine LUT

int allocW = 0, allocH = 0;
int planeStride = 0;          // w rounded up to eight, so every plane starts vector aligned
int starTop = 0, starBot = 0; // rows a star can touch, inclusive
int starN = NSTAR;            // stars actually drawn, from the density parameter
int skyBase = 22;             // palette index the sky starts from, from Sky tone
int lastBright = -1;
int lastHaze = -1;
uint32_t lastThemeGen = 0xFFFFFFFFu;

// Hot slab at w = 480, h = 480, each table's own size and then what allocHot
// rounds it to: heightQ 2,880, tileBound 360 (368), glowTab 184 (192), smooth
// 514 (528), palette 512 (512), stars 1,080 (1,088), layers 120 (128), work
// 400 (400), planes 2,880. The star and haze tables are sized for the top of
// their parameter ranges, not for the defaults. Total 8,976 B of the 9,216 B
// an animation may take, with no fallback to PSRAM and the slab back to
// empty after release(). PSRAM holds starDef, 2,160 B, which frame() reads
// and band() never does. Every table read per pixel or per row comes from
// allocHot, and nothing is allocated in frame() or band().
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

// The page's q565r and q565g: an RGB565 colour expanded to the 8-bit
// channels its palette holds, the high bits repeated into the low ones.
BGANIM_INLINE int chanR(uint16_t c) {
    const int v = (c >> 11) & 0x1F;
    return (v << 3) | (v >> 2);
}
BGANIM_INLINE int chanG(uint16_t c) {
    const int v = (c >> 5) & 0x3F;
    return (v << 2) | (v >> 4);
}
BGANIM_INLINE int chanB(uint16_t c) {
    const int v = c & 0x1F;
    return (v << 3) | (v >> 2);
}

// pcMix32 on one channel: d + (((f - d) * a) >> 8), a in 0..256, and the
// shift is arithmetic, so a negative step floors, as JS's >> does.
BGANIM_INLINE int lerp8(int d, int f, int a) { return d + (((f - d) * a) >> 8); }

bool init(int w, int h) {
    if (w <= 0 || w > MAX_W || h <= 0 || h > 480) return false;
    if (heightQ != nullptr && w == allocW && h == allocH) return true;
    release();
    allocW = w;
    allocH = h;
    planeStride = (w + 7) & ~7;
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
    layers = static_cast<Layer *>(allocHot(LAYERS * sizeof(Layer)));
    work = static_cast<uint16_t *>(allocHot(WORK_N * sizeof(uint16_t)));
    planes = static_cast<uint16_t *>(allocHot(3 * static_cast<size_t>(planeStride) * sizeof(uint16_t)));
    starDef = static_cast<StarDef *>(alloc(NSTAR_MAX * sizeof(StarDef)));
    // One test over every pointer, and release() on any failure, so a half
    // finished init() leaves the slab exactly as it found it and the retry
    // BgAnim.h promises starts from scratch.
    if (heightQ == nullptr || tileBound == nullptr || glowTab == nullptr || smooth == nullptr || palette == nullptr ||
        stars == nullptr || layers == nullptr || work == nullptr || planes == nullptr || starDef == nullptr) {
        release();
        return false;
    }
    // The page's Math.round(256 * t * t * (3 - 2 * t)) in double, in its
    // own multiplication order: a float form of this table rounded one
    // entry the other way.
    for (int k = 0; k < SS_N; k++) {
        const double t = k / 256.0;
        smooth[k] = static_cast<uint16_t>(256.0 * t * t * (3.0 - 2.0 * t) + 0.5);
    }
    // The pack kernel's constants: ones, and the multipliers that put the
    // red and green fields back in place (there is no vector left shift).
    uint16_t *c = work + WK_CT;
    for (int i = 0; i < 8; i++) {
        c[i] = 1;
        c[8 + i] = 2048;
        c[16 + i] = 32;
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
// Built in double, in the page's multiplication order.
void buildGlow(int gn) {
    for (int k = 0; k < GLOW_SIZE; k++) {
        const double u = k < gn * 4 ? k / static_cast<double>(gn * 4) : 1.0;
        glowTab[k] = static_cast<uint8_t>(GLOW_PEAK * (1.0 - u) * (1.0 - u) + 0.5);
    }
}

// True when v sits within tol of a whole number, where a float and the
// page's double can floor to different integers.
BGANIM_INLINE bool nearInteger(float v, float tol) {
    const float f = v - floorf(v);
    return f < tol || f > 1.0f - tol;
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[BG_ANIM_PARAMS]) {
    if (lastBright != p[3] || lastThemeGen != themeGen()) {
        // The page's own brightness, 176 + round(p[3] * 0.8), on top of the
        // shared theme tone. round(x) is floor(x + 1/2), which for p in
        // 0..100 is exactly (p * 4 + 2) / 5 in integers.
        buildThemeRamp(palette, static_cast<uint16_t>(176 + (static_cast<int>(p[3]) * 4 + 2) / 5));
        lastBright = p[3];
        lastThemeGen = themeGen();
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
    skyBase = 22 * static_cast<int>(p[6]) / 50;
    // Speed calibration (gm-33fm): the clock runs at 1.875x of the original
    // rate, so Speed 50 gives about the same visible movement here as on
    // every other animation. The clock is the page's double, pow(2, ...)
    // included, so the two stay bit identical at any speed. The Speed
    // parameter, its label and its default of 50 are unchanged.
    const double sm = pow(2.0, (static_cast<int>(p[0]) - 50) / 18.2) * 1.875;
    const double tt = static_cast<double>(tMs) * sm;
    const double relief = 0.5 + p[1] * 0.012;
    const double contrast = 0.6 + p[2] * 0.008;
    // Star density, p[7]: none at 0, the page's 90 at 50, 180 at 100. The
    // stars drawn are always the first starN of one fixed field, so raising
    // the slider adds stars and never moves the ones already there.
    starN = static_cast<int>(p[7]) * 9 / 5;
    if (starN > NSTAR_MAX) starN = NSTAR_MAX;

    // Stars in double, as the page has them: a float drift put the column
    // fraction one unit off where the sum sat on a 1/256 boundary.
    const double drift = fmod(tt * 0.0006, static_cast<double>(w)); // 0.6 px/s at speed 50
    const uint32_t tw = static_cast<uint32_t>(static_cast<int64_t>(tt * 0.0009)); // twinkle phase
    for (int i = 0; i < starN; i++) {
        const StarDef &s = starDef[i];
        const double xf = static_cast<double>(s.x) + drift; // below 2w: s.x < w and drift < w
        const int xi = static_cast<int>(xf);
        Star &out = stars[i];
        out.x = static_cast<uint16_t>(xi >= w ? xi - w : xi);
        out.y = static_cast<uint8_t>(s.y);
        out.fraction = static_cast<uint8_t>(static_cast<int>((xf - xi) * 256.0));
        const int idx = s.bright + ((sine[(s.phase + tw) & (SIN_N - 1)] * 9) >> 9);
        out.colour = palette[idx > 255 ? 255 : idx];
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
        s.idx0 = static_cast<int>(floor(30.0 + (d.index - 30) * contrast + 0.5));
        s.glow = palette[s.idx0 + 26 > 255 ? 255 : s.idx0 + 26];
        const double a1 = d.a1 * relief, a2 = d.a2 * relief;
        // The page truncates y - topRow toward zero before scaling it, so
        // both brackets of the top row are needed, not one rounding.
        const double top = base - (a1 + a2);
        s.topFloor = static_cast<int>(floor(top));
        s.topCeil = static_cast<int>(ceil(top));
        // The page evaluates ((x + scroll) * k + q) | 0 & 1023 per column in
        // doubles, and (SIN[i1] * a1 + SIN[i2] * a2) >> 1 on the result. A
        // double per column is a software helper call on the device, so
        // each column runs in float with the phase reduced once per layer
        // (the float's rounding is then under 1/2048 of a table entry, and
        // the height sum's under 1/128 of a unit), and only a column whose
        // float lands within 1/512 of a table boundary, or whose sum lands
        // within 1/64 of a whole number, is recomputed in double in the
        // page's own expression order. At the bench's golden frames those
        // ties were the only heights that differed.
        const double scroll = tt * d.speed / 1000.0;
        const double q1 = tt * d.rate1 * 1024, q2 = tt * d.rate2 * 1024;
        const double k1 = 1024.0 / d.period1, k2 = 1024.0 / d.period2;
        const float b1 = static_cast<float>(fmod(scroll * k1 + q1, static_cast<double>(SIN_N)));
        const float b2 = static_cast<float>(fmod(scroll * k2 + q2, static_cast<double>(SIN_N)));
        const float k1f = static_cast<float>(k1), k2f = static_cast<float>(k2);
        const float a1f = static_cast<float>(a1), a2f = static_cast<float>(a2);
        int16_t *hq = heightQ + l * w;
        int16_t *tb = tileBound + l * MAX_TILES * 2;
        int gmin = 32767, gmax = -32768;
        for (int t = 0; t < nT; t++) {
            const int xs = t * TILE;
            const int xe = xs + TILE < w ? xs + TILE : w;
            int lo = 32767, hi = -32768;
            for (int x = xs; x < xe; x++) {
                const float v1 = x * k1f + b1, v2 = x * k2f + b2;
                int i1, i2;
                if (nearInteger(v1, 1.0f / 512.0f)) {
                    i1 = static_cast<int>(static_cast<int64_t>(floor((x + scroll) * k1 + q1)) & (SIN_N - 1));
                } else {
                    i1 = static_cast<int>(floorf(v1)) & (SIN_N - 1);
                }
                if (nearInteger(v2, 1.0f / 512.0f)) {
                    i2 = static_cast<int>(static_cast<int64_t>(floor((x + scroll) * k2 + q2)) & (SIN_N - 1));
                } else {
                    i2 = static_cast<int>(floorf(v2)) & (SIN_N - 1);
                }
                // The page truncates the weighted sine sum toward zero before
                // halving it, and JS's >> is an arithmetic shift, so a
                // negative sum floors. Both are reproduced here.
                const float sf = sine[i1] * a1f + sine[i2] * a2f;
                int v;
                if (nearInteger(sf, 1.0f / 64.0f)) {
                    v = static_cast<int>(sine[i1] * a1 + sine[i2] * a2) >> 1;
                } else {
                    v = static_cast<int>(sf) >> 1;
                }
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

// The sky's palette index for one row: a quadratic ramp from skyBase that
// lifts 44 toward the far ridge line, dithered over four rows. The page
// truncates (44 * (y / h) * (y / h) * 256) in double; the integer form here
// is the same floor at every row, because whenever the exact value is a
// whole number (y a multiple of 15 at h = 480 or 240) y / h is exact in
// binary and every product with it is too, and everywhere else the value
// is at least 1/225 away from a whole number, far beyond a double's error.
BGANIM_INLINE int skyIndex(int y) {
    // The numerator is under 2^32 for any height up to 480 and the height
    // is never zero.
    const uint32_t skyQ = 44u * static_cast<uint32_t>(y) * static_cast<uint32_t>(y) * 256u /
                          (static_cast<uint32_t>(allocH) * static_cast<uint32_t>(allocH));
    const int idx = (skyBase * 256 + static_cast<int>(skyQ) + ditherY(y)) >> 8;
    return idx > 255 ? 255 : idx;
}

#if GM_BGANIM_HILLS_ASM
#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)

// ---------------------------------------------------------------------------
// Xtensa LX7 kernels. All three use the PIE unit (CP3), eight 16-bit lanes
// per q register. band() runs on the SleepAnim task and never in an ISR, so
// FreeRTOS saves the q registers and SAR lazily per task and an ssai hoisted
// above a loop survives an interrupt or a task switch. The compiler never
// allocates q registers, so these blocks use q0 to q7 freely and there is no
// constraint syntax to declare them. No kernel writes CPENABLE.
//
// ee.vld.128.ip and ee.vst.128.ip mask the low four address bits silently
// instead of trapping, so every span these kernels touch is 16-byte aligned
// by construction: allocHot() hands back 16-byte aligned tables, the planes
// and the scratch sub-buffers sit at multiples of 16 B inside them, and the
// callers below pay a scalar prefix or fall back to scalar code rather than
// hand a kernel a destination that is not aligned.
// ---------------------------------------------------------------------------

// Constant fill, eight lanes per store. colV holds the value in all eight
// lanes, so the whole body is one vector store and the loop has no back edge.
// This is the animation's largest single cost: the sky is a full-width fill on
// every row and each layer fills the runs it covers, which is about 575,000
// pixels a frame at 480 x 480, three planes each.
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

// dst = dst + (((fg - dst) * a) >> 8) over eight lanes at a time, on one
// plane of 8-bit channel values held in 16-bit lanes, a in 0..256 per lane.
// This is pcMix32's channel arithmetic exactly: ee.vsubs.s16 gives the
// signed step, at most 255 in magnitude, ee.vmul.s16 multiplies it by the
// weight and shifts the 32-bit product right by SAR arithmetically, which is
// the floor JS's >> takes on a negative product, and ee.vadds.s16 adds the
// result back. Nothing here can saturate: the largest product is 65,280 and
// the result is always 0..255. Nine instructions per eight lanes, three
// planes per pixel group.
GM_ANIM_IRAM __attribute__((noinline)) void hillsLerp8(uint16_t *dst, const uint16_t *fg, const uint16_t *aQ8,
                                                       int groups) {
    uint16_t *rd = dst;
    uint16_t *wr = dst;
    const uint16_t *fv = fg;
    const uint16_t *av = aQ8;
    // rd and wr are the same address at entry and are separate registers
    // because ee.vld.128.ip and ee.vst.128.ip each post-increment their own
    // pointer, and the group's destination has to be read before the other
    // pointers move and written after.
    asm volatile("ssai 8\n"
                 "loopnez %[n], 1f\n"
                 "ee.vld.128.ip q2, %[av], 16\n" // a
                 "ee.vld.128.ip q0, %[fv], 16\n" // fg
                 "ee.vld.128.ip q1, %[rd], 16\n" // dst
                 "ee.vsubs.s16 q0, q0, q1\n"     // fg - dst, -255..255
                 "ee.vmul.s16 q0, q0, q2\n"      // ((fg - dst) * a) >> 8
                 "ee.vadds.s16 q0, q0, q1\n"     // dst + step
                 "ee.vst.128.ip q0, %[wr], 16\n"
                 "1:\n"
                 : [rd] "+r"(rd), [wr] "+r"(wr), [fv] "+r"(fv), [av] "+r"(av)
                 : [n] "r"(groups)
                 : "memory");
}

// Pack eight pixels from the three planes into RGB565: (r >> 3) << 11 |
// (g >> 2) << 5 | (b >> 3). ee.vmul.u16 only shifts right, product >> SAR
// with the low 16 bits kept, so the channel shifts multiply by the ones
// vector at SAR 3 or 2, and the repositioning multiplies by 2048 and 32 at
// SAR 0. The three constants are loaded once and pinned in q5 to q7.
// Twelve instructions per eight pixels, once per output row.
GM_ANIM_IRAM __attribute__((noinline)) void hillsPack8(uint16_t *dst, const uint16_t *r, const uint16_t *g,
                                                       const uint16_t *b, const uint16_t *ct, int groups) {
    uint16_t *wr = dst;
    const uint16_t *rp = r;
    const uint16_t *gp = g;
    const uint16_t *bp = b;
    const uint16_t *cp = ct;
    asm volatile("ee.vld.128.ip q5, %[cp], 16\n" // ones
                 "ee.vld.128.ip q6, %[cp], 16\n" // 2048
                 "ee.vld.128.ip q7, %[cp], 16\n" // 32
                 "loopnez %[n], 1f\n"
                 "ee.vld.128.ip q0, %[rp], 16\n"
                 "ee.vld.128.ip q1, %[gp], 16\n"
                 "ee.vld.128.ip q2, %[bp], 16\n"
                 "ssai 3\n"
                 "ee.vmul.u16 q0, q0, q5\n" // r >> 3
                 "ee.vmul.u16 q2, q2, q5\n" // b >> 3
                 "ssai 2\n"
                 "ee.vmul.u16 q1, q1, q5\n" // g >> 2
                 "ssai 0\n"
                 "ee.vmul.u16 q0, q0, q6\n" // red << 11
                 "ee.vmul.u16 q1, q1, q7\n" // green << 5
                 "ee.orq q0, q0, q1\n"
                 "ee.orq q0, q0, q2\n"
                 "ee.vst.128.ip q0, %[wr], 16\n"
                 "1:\n"
                 : [wr] "+r"(wr), [rp] "+r"(rp), [gp] "+r"(gp), [bp] "+r"(bp), [cp] "+r"(cp)
                 : [n] "r"(groups)
                 : "memory");
}

#else
// Portable twins of the three kernels above: same names, same signatures,
// same arithmetic, so the dispatching code below is one piece of source
// whichever branch compiled. This branch is what the host bench and the
// fuzzers run, and what GM_BGANIM_NO_ASM selects on the device. All three
// are copied from tools/qemubench/tests/anim_hills/main.c, the plain C
// references that file checks the kernels against under QEMU.
void hillsFill8(uint16_t *dst, const uint16_t *colV, int groups) {
    const uint16_t c = colV[0];
    const int n = groups * 8;
    for (int i = 0; i < n; i++) dst[i] = c;
}
void hillsLerp8(uint16_t *dst, const uint16_t *fg, const uint16_t *aQ8, int groups) {
    const int n = groups * 8;
    for (int i = 0; i < n; i++) {
        const int d = dst[i];
        dst[i] = static_cast<uint16_t>(d + (((fg[i] - d) * aQ8[i]) >> 8));
    }
}
void hillsPack8(uint16_t *dst, const uint16_t *r, const uint16_t *g, const uint16_t *b, const uint16_t *,
                int groups) {
    const int n = groups * 8;
    for (int i = 0; i < n; i++) {
        dst[i] = static_cast<uint16_t>(((r[i] >> 3) << 11) | ((g[i] >> 2) << 5) | (b[i] >> 3));
    }
}
#endif // __XTENSA__ && !GM_BGANIM_NO_ASM
#endif // GM_BGANIM_HILLS_ASM

// Fill n pixels of the three planes, from column x, with one colour. The
// vector path needs a 16-byte aligned destination, so it pays a scalar
// prefix of up to seven pixels to get there and a scalar tail for the last
// group; below 16 pixels the prefix would eat the whole run, so short runs
// stay scalar. The three planes share one alignment because the stride is
// a multiple of eight lanes. colV holds the three channel values broadcast
// over a tile, at WK_COL or WK_GLOW.
template <bool Asm>
BGANIM_INLINE void fillRun(int x, int n, const uint16_t *colV, int r, int g, int b) {
    uint16_t *pr = planes + x;
    uint16_t *pg = pr + planeStride;
    uint16_t *pb = pg + planeStride;
#if GM_BGANIM_HILLS_ASM
    if (Asm && n >= 16) {
        const int pre = static_cast<int>((-reinterpret_cast<uintptr_t>(pr)) & 15u) >> 1;
        for (int i = 0; i < pre; i++) {
            pr[i] = static_cast<uint16_t>(r);
            pg[i] = static_cast<uint16_t>(g);
            pb[i] = static_cast<uint16_t>(b);
        }
        const int groups = (n - pre) >> 3;
        hillsFill8(pr + pre, colV, groups);
        hillsFill8(pg + pre, colV + TILE, groups);
        hillsFill8(pb + pre, colV + 2 * TILE, groups);
        for (int i = pre + (groups << 3); i < n; i++) {
            pr[i] = static_cast<uint16_t>(r);
            pg[i] = static_cast<uint16_t>(g);
            pb[i] = static_cast<uint16_t>(b);
        }
        return;
    }
#else
    (void)colV;
#endif
    for (int i = 0; i < n; i++) {
        pr[i] = static_cast<uint16_t>(r);
        pg[i] = static_cast<uint16_t>(g);
        pb[i] = static_cast<uint16_t>(b);
    }
}

// dst = lerp8(dst, fg, a) over n pixels of three planes at stride s. Every
// caller hands over vector aligned planes: the row planes offset by a whole
// tile, or scratch inside work. Anything past the last whole group takes the
// scalar loop, which is the same integer either way.
template <bool Asm>
BGANIM_INLINE void blendRun(uint16_t *dst, int ds, const uint16_t *fg, int fs, const uint16_t *aQ8, int n) {
    int i0 = 0;
#if GM_BGANIM_HILLS_ASM
    if (Asm) {
        const int groups = n >> 3;
        hillsLerp8(dst, fg, aQ8, groups);
        hillsLerp8(dst + ds, fg + fs, aQ8, groups);
        hillsLerp8(dst + 2 * ds, fg + 2 * fs, aQ8, groups);
        i0 = groups << 3;
    }
#endif
    for (int i = i0; i < n; i++) {
        dst[i] = static_cast<uint16_t>(lerp8(dst[i], fg[i], aQ8[i]));
        dst[ds + i] = static_cast<uint16_t>(lerp8(dst[ds + i], fg[fs + i], aQ8[i]));
        dst[2 * ds + i] = static_cast<uint16_t>(lerp8(dst[2 * ds + i], fg[2 * fs + i], aQ8[i]));
    }
}

// Broadcast one colour's three channels over a tile of lanes at v.
BGANIM_INLINE void broadcast(uint16_t *v, uint16_t colour) {
    const int r = chanR(colour), g = chanG(colour), b = chanB(colour);
    for (int i = 0; i < TILE; i++) {
        v[i] = static_cast<uint16_t>(r);
        v[TILE + i] = static_cast<uint16_t>(g);
        v[2 * TILE + i] = static_cast<uint16_t>(b);
    }
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

// Pack the row planes into the output row. The vector path needs a 16-byte
// aligned destination, which the panel's 64-byte aligned band buffer and a
// width that is a multiple of eight both give; anything else, and the tail
// past the last whole group, takes the scalar form of the same shift and or.
template <bool Asm> BGANIM_INLINE void packRow(uint16_t *out, int w) {
    const uint16_t *pr = planes;
    const uint16_t *pg = pr + planeStride;
    const uint16_t *pb = pg + planeStride;
    int i0 = 0;
#if GM_BGANIM_HILLS_ASM
    if (Asm && (reinterpret_cast<uintptr_t>(out) & 15u) == 0) {
        const int groups = w >> 3;
        hillsPack8(out, pr, pg, pb, work + WK_CT, groups);
        i0 = groups << 3;
    }
#endif
    for (int i = i0; i < w; i++) {
        out[i] = static_cast<uint16_t>(((pr[i] >> 3) << 11) | ((pg[i] >> 2) << 5) | (pb[i] >> 3));
    }
}

template <bool Asm> GM_ANIM_IRAM void renderRow(uint16_t *out, int y, int w, int nT) {
    uint16_t *aQ8 = work + WK_A;
    uint16_t *bQ8 = work + WK_B;
    uint16_t *tgt = work + WK_TGT;
    uint16_t *colV = work + WK_COL;
    uint16_t *glowV = work + WK_GLOW;

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
        // Sky: a quadratic ramp that lifts toward the far ridge line, with
        // the dither already in the index.
        const uint16_t sky = palette[skyIndex(y)];
        broadcast(colV, sky);
        fillRun<Asm>(0, w, colV, chanR(sky), chanG(sky), chanB(sky));

        if (y >= starTop && y <= starBot) {
            uint16_t *pr = planes;
            uint16_t *pg = pr + planeStride;
            uint16_t *pb = pg + planeStride;
            for (int i = 0; i < starN; i++) {
                const Star &s = stars[i];
                const int dy = y - s.y;
                if (dy < -1 || dy > 1) continue;
                const int a = dy == 0 ? 200 : 58; // the core, then the cross arms
                const int x0 = s.x, x1 = s.x + 1 == w ? 0 : s.x + 1;
                const int a0 = (a * (256 - s.fraction)) >> 8, a1 = (a * s.fraction) >> 8;
                const int r = chanR(s.colour), g = chanG(s.colour), b = chanB(s.colour);
                pr[x0] = static_cast<uint16_t>(lerp8(pr[x0], r, a0));
                pg[x0] = static_cast<uint16_t>(lerp8(pg[x0], g, a0));
                pb[x0] = static_cast<uint16_t>(lerp8(pb[x0], b, a0));
                pr[x1] = static_cast<uint16_t>(lerp8(pr[x1], r, a1));
                pg[x1] = static_cast<uint16_t>(lerp8(pg[x1], g, a1));
                pb[x1] = static_cast<uint16_t>(lerp8(pb[x1], b, a1));
            }
        }
    } else {
        broadcast(colV, firstCol);
        fillRun<Asm>(0, w, colV, chanR(firstCol), chanG(firstCol), chanB(firstCol));
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
        const int cr = chanR(colour), cg = chanG(colour), cb = chanB(colour);
        const int16_t *hq = heightQ + l * w;
        const int16_t *tb = tileBound + l * MAX_TILES * 2;
        broadcast(colV, colour);
        broadcast(glowV, s.glow);

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
                    fillRun<Asm>(pos, xs - pos, colV, cr, cg, cb);
                    pos = xs;
                    covered = false;
                    n++;
                }
            } else { // the ridge crosses this tile, so read its columns
                const int xe = xs + TILE < w ? xs + TILE : w;
                for (int x = t != 0 ? xs : 1; x < xe; x++) {
                    const bool cur = hq[x] < covThr;
                    if (cur != covered) {
                        if (covered) fillRun<Asm>(pos, x - pos, colV, cr, cg, cb);
                        pos = x;
                        covered = cur;
                        if (++n >= CAP) break;
                    }
                }
            }
        }
        if (covered) fillRun<Asm>(pos, w - pos, colV, cr, cg, cb);

        // The anti-aliased ridge line and the haze band above it, then the
        // fade below it, over whatever is already on the row.
        for (int t = 0; t < nT; t++) {
            const int lo = tb[2 * t], hi = tb[2 * t + 1];
            if (hi < covThr || lo >= edgeHi) continue;
            const int xs = t * TILE;
            const int nn = xs + TILE < w ? TILE : w - xs;
            if (lo > yq) { // wholly above the ridge: haze over one colour
                hillsHaze(bQ8, hq + xs, yq, edgeHi, nn);
                blendRun<Asm>(planes + xs, planeStride, glowV, TILE, bQ8, nn);
                continue;
            }
            hillsWeights(aQ8, bQ8, hq + xs, yr, s.gn, s.fade, s.fadeInv, nn);
            memcpy(tgt, glowV, 3 * TILE * sizeof(uint16_t));
            blendRun<Asm>(tgt, TILE, colV, TILE, aQ8, nn);              // haze colour walking to the layer
            blendRun<Asm>(planes + xs, planeStride, tgt, TILE, bQ8, nn); // that cover over the row
        }
    }
    packRow<Asm>(out, w);
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
    releaseTable(layers, LAYERS * sizeof(Layer));
    releaseTable(work, WORK_N * sizeof(uint16_t));
    releaseTable(planes, 3 * static_cast<size_t>(planeStride) * sizeof(uint16_t));
    releaseTable(starDef, NSTAR_MAX * sizeof(StarDef));
    sine = nullptr;
    allocW = allocH = 0;
    planeStride = 0;
    starTop = starBot = 0;
    starN = NSTAR;
    skyBase = 22;
    lastBright = -1;
    lastHaze = -1;
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
