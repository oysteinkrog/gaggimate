#ifndef GAGGIMATE_SIM

// "Fireflies": entry 9 of tools/animbench/web/anim_bench.html, the page the
// owner approved the look on. Soft motes on sum-of-sines wander paths with
// individual pulses and an occasional synchronized shimmer ring, drawn as
// additive glow sprites over a radial dusk gradient. Design: anim-particles
// (Fable), 2026-08-15.
//
// This file draws the page's picture, pixel for pixel after RGB565
// quantisation (gm-pciz, 2026-09-12). The version before it shared the
// page's design and none of its arithmetic: an xorshift swarm where the
// page seeds mulberry32, a per-scanline vertical gradient where the page's
// is radial, a 64-entry falloff table and a 5-6-5 additive blend where the
// page blends per pixel in 8-bit float with a brightness that runs up to
// 1.3, so 28,700 to 29,900 of the 230,400 pixels of each golden frame
// differed and no sprite sat on the page's position. What is here now:
//
// - The swarm is the page's: mulberry32(0x9e3779b9), the same draws in the
//   same order, every particle constant a double. The generator is not
//   reseeded when the count changes, because the page's is not.
// - The background is the page's radial gradient, themeRGB(8) at the centre
//   to themeRGB(3) at 0.46 of the panel and beyond, rounded to 8 bits the
//   way a Uint8ClampedArray store rounds (half to even). The gradient moves
//   a channel by at most a few units over the whole panel, so a row of it
//   is a handful of runs, one per ring where some channel steps. The rings
//   are found once per theme, and for every row the column where each
//   ring crosses is found by evaluating the page's own double expression at
//   the candidate columns and stored in PSRAM (rowCross), one byte per ring
//   per row. Rendering a row is then a run walk: the PIE fill per run and
//   no per-pixel work. 128 rings are the cap (RING_CAP); the fleet's themes
//   need 4 to 30, and a theme past the cap keeps its innermost rings.
// - A sprite's pixels are computed in float with the page's formula
//   (distance, falloff, core mix, additive 8-bit accumulate) and a pixel
//   whose float value lands within 2e-3 of a rounding boundary, within
//   1e-5 of the sprite's rim or within 1e-6 of the 0.004 alpha cutoff is
//   recomputed in double in the page's expression order. The float error
//   is under 3e-4 of a unit at the worst pixel (the core mix's slope is
//   about 1,500 units per unit of normalised distance, and the float
//   distance is exact to a few ulp because the sprite centre is split into
//   a whole pixel and a fraction), so the fallback is what keeps the two
//   sides identical and it runs on about one pixel in 500. The additive
//   accumulate chains through the page's rounded 8-bit store, so the
//   sprites accumulate in three 16-bit planes over the union of their
//   spans on the row, and only those spans are packed to RGB565.
// - The per-frame constants (positions, brightness, radius) are double,
//   with sin, pow and exp from libm: about 210 double transcendental calls
//   a frame at the default count. The picture depends on them to the last
//   bit and there is no cheaper form that does.
//
// Measured against the page rendered at the bench's three golden frame
// times and quantised to RGB565: zero differing pixels. The host bench
// band is 0.56 ms a frame against the old port's 0.15, all of it the
// per-pixel float sprite work, which is the price of the page's model: an
// 8-bit additive over a brightness up to 1.3 has no table form that
// survives the rounding.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace {
using namespace bganim;

constexpr int FF_MAX = 40;
constexpr int RING_CAP = 128;   // background rings a theme may need
constexpr int NO_CROSS = 255;   // rowCross value: this ring does not reach this row
constexpr int MAX_RUNS = 2 * RING_CAP + 2;

// One particle, every field the page's double.
struct Firefly {
    double homeX, homeY;
    double wx1, wx2, wy1, wy2;
    double ax1, ax2, ay1, ay2;
    double px1, px2, py1, py2;
    double pulseFreq, pulsePhase;
    double radialNorm, size, hueMix;
};

// What band() reads per sprite: the double constants for the fallback, the
// float copies for the fast path, the bounding box, and the glow colour.
struct FfDraw {
    double x, y, R, brightness;
    float xFrac, yFrac, invR, brF;
    float gr, gg, gb;
    int16_t xInt, yInt, x0, x1, y0, y1;
};

// A background ring: the k-th step of one channel, at the radius where the
// page's value sits on a rounding boundary. Only the rebuild reads T and
// step; band() reads chan and sign through ringChan/ringSign.
struct Ring {
    double T;
    uint8_t chan, step;
    int8_t sign;
};

struct Run {
    int16_t x0, x1;
    uint16_t c565;
    uint8_t r, g, b, pad;
};

Firefly *ff = nullptr;      // [FF_MAX]            PSRAM, frame() only
Ring *rings = nullptr;      // [RING_CAP]          PSRAM, rebuild only
uint8_t *rowCross = nullptr; // [h * RING_CAP]     PSRAM, one row read per row
FfDraw *draws = nullptr;    // [FF_MAX]            hot, read per band per sprite
uint16_t *planes = nullptr; // [3 * planeStride]   hot, 8-bit channels in 16-bit lanes
Run *runs = nullptr;        // [MAX_RUNS]          hot, the row's background runs
uint8_t *ringChan = nullptr; // [RING_CAP]         hot
int8_t *ringSign = nullptr;  // [RING_CAP]         hot
uint8_t *crossM = nullptr;   // [RING_CAP]         hot, sorted crossings of one row
uint8_t *crossK = nullptr;   // [RING_CAP]         hot, their ring indices
int16_t *spans = nullptr;    // [FF_MAX * 2]       hot, sprite spans on one row

int allocW = 0, allocH = 0, planeStride = 0;
int ffCount = 0;
int builtCount = -1;
int ringN = 0;
uint8_t c0[3], c1[3], coreC[3];
double cx = 0, cy = 0, rMax = 0;
double halo = 2.0;
float haloF = 2.0f;
bool halo2 = true;
int xl0 = 0, xr0 = 0;     // the columns either side of the centre
double fracX = 0;            // 0 for an even width, 0.5 for an odd one
uint32_t rng = 0x9e3779b9u;
uint32_t lastThemeGen = 0xFFFFFFFFu;

// Hot slab at w = 480: draws 40 * 72 = 2,880 B, planes 2,880, runs 258 *
// 8 = 2,064, ringChan 128, ringSign 128, crossM 128, crossK 128, spans 160:
// 8,496 B of the 9,216 B an animation may take. PSRAM holds the particles
// (6,080 B), the rings (2,048 B) and rowCross (61,440 B at h = 480), of
// which band() reads ringN bytes per row.
void release();

// The page's mulberry32, in uint32 arithmetic (see AnimHills.cpp), and its
// 0..1 fraction.
uint32_t mulberry(uint32_t &seed) {
    seed += 0x6D2B79F5u;
    uint32_t t = (seed ^ (seed >> 15)) * (1u | seed);
    t = (t + (t ^ (t >> 7)) * (61u | t)) ^ t;
    return t ^ (t >> 14);
}
double rand01() { return mulberry(rng) / 4294967296.0; }

// The page's pmul: a multiplier on a constant this animation used to
// hard-code, lo at 0, exactly 1 at 50, hi at 100.
double pmul(int v, double lo, double hi) {
    if (v == 50) return 1.0;
    return 1 + ((v - 50) / 50.0) * (v < 50 ? (1 - lo) : (hi - 1));
}

// A Uint8ClampedArray store: clamp to 0..255, round half to even.
int clampRound(double v) {
    if (v <= 0) return 0;
    if (v >= 255) return 255;
    const double f = floor(v), d = v - f;
    if (d < 0.5) return static_cast<int>(f);
    if (d > 0.5) return static_cast<int>(f) + 1;
    const int i = static_cast<int>(f);
    return (i & 1) ? i + 1 : i;
}

// The page's background channel at one pixel, in its own expression order:
// n = min(1, sqrt(dx * dx + dy * dy) / rMax), c0 + (c1 - c0) * n, stored.
int pageBg(int c, double dx, double dy) {
    double n = sqrt(dx * dx + dy * dy) / rMax;
    if (n > 1) n = 1;
    return clampRound(c0[c] + (c1[c] - c0[c]) * n);
}

// The column offset from the centre where ring k first shows on the row at
// dy: the smallest m at which the page's rounded channel has taken the
// ring's step. The page's value is monotone in the distance (every
// operation in it is), so the search starts two columns inside the ring's
// nominal radius and walks out, or walks in if it is already past.
int crossOf(int k, double dy) {
    const Ring &rg = rings[k];
    const int target = c0[rg.chan] + rg.sign * rg.step;
    const int mMax = xl0 > allocW - 1 - xr0 ? xl0 : allocW - 1 - xr0;
    auto reached = [&](int m) {
        const int v = pageBg(rg.chan, m + fracX, dy);
        return rg.sign > 0 ? v >= target : v <= target;
    };
    const double inside = rg.T * rg.T - dy * dy;
    int m = inside > 0 ? static_cast<int>(floor(sqrt(inside))) - 2 : 0;
    if (m < 0) m = 0;
    if (m > mMax) m = mMax;
    if (reached(m)) {
        while (m > 0 && reached(m - 1)) m--;
        return m;
    }
    while (m < mMax) {
        m++;
        if (reached(m)) return m;
    }
    return NO_CROSS;
}

// Everything that depends on the theme: the gradient's end colours, its
// rings and their row crossings, the sprite core colour and each
// particle's glow colour. The page rebuilds all of it together on a theme
// change and on a count change.
void rebuildTheme() {
    themeRGB(8, c0);
    themeRGB(3, c1);
    themeRGB(250, coreC);
    ringN = 0;
    for (int c = 0; c < 3; c++) {
        const int delta = c1[c] - c0[c];
        const int steps = delta < 0 ? -delta : delta;
        for (int j = 1; j <= steps && ringN < RING_CAP; j++) {
            Ring &rg = rings[ringN++];
            rg.T = ((j - 0.5) / steps) * rMax;
            rg.chan = static_cast<uint8_t>(c);
            rg.step = static_cast<uint8_t>(j);
            rg.sign = static_cast<int8_t>(delta < 0 ? -1 : 1);
        }
    }
    // Innermost first, so a theme past the cap keeps the rings nearest the
    // centre and the outer rim merges into the last one.
    for (int i = 1; i < ringN; i++) {
        const Ring t = rings[i];
        int j = i - 1;
        while (j >= 0 && rings[j].T > t.T) {
            rings[j + 1] = rings[j];
            j--;
        }
        rings[j + 1] = t;
    }
    for (int k = 0; k < ringN; k++) {
        ringChan[k] = rings[k].chan;
        ringSign[k] = rings[k].sign;
    }
    for (int y = 0; y < allocH; y++) {
        const double dy = y - cy;
        uint8_t *row = rowCross + static_cast<size_t>(y) * RING_CAP;
        for (int k = 0; k < ringN; k++) row[k] = static_cast<uint8_t>(crossOf(k, dy));
    }
    for (int i = 0; i < builtCount; i++) {
        uint8_t c[3];
        themeRGB(185 + static_cast<int>(ff[i].hueMix * 70), c);
        draws[i].gr = c[0];
        draws[i].gg = c[1];
        draws[i].gb = c[2];
    }
}

// The page's buildSwarm: the same draws from the shared generator, in the
// order its object literal evaluates them.
void buildSwarm(int count) {
    for (int i = 0; i < count; i++) {
        Firefly &f = ff[i];
        const double theta = rand01() * M_PI * 2;
        const double rr = rMax * sqrt(rand01()) * 0.92;
        const double basePeriod = 9000 + rand01() * 5000;
        const double w1 = 2 * M_PI / basePeriod;
        f.homeX = cx + cos(theta) * rr;
        f.homeY = cy + sin(theta) * rr;
        f.wx1 = w1;
        f.wx2 = w1 * 1.618 * (0.85 + rand01() * 0.3);
        f.wy1 = w1 * 1.13 * (0.9 + rand01() * 0.2);
        f.wy2 = w1 * 1.414 * (0.85 + rand01() * 0.3);
        f.ax1 = 16 + rand01() * 10;
        f.ax2 = 7 + rand01() * 6;
        f.ay1 = 16 + rand01() * 10;
        f.ay2 = 7 + rand01() * 6;
        f.px1 = rand01() * M_PI * 2;
        f.px2 = rand01() * M_PI * 2;
        f.py1 = rand01() * M_PI * 2;
        f.py2 = rand01() * M_PI * 2;
        f.pulseFreq = 2 * M_PI / (2400 + rand01() * 3600);
        f.pulsePhase = rand01() * M_PI * 2;
        f.radialNorm = rr / rMax;
        f.size = 0.8 + rand01() * 0.5;
        f.hueMix = rand01();
    }
    builtCount = count;
}

bool init(int w, int h) {
    if (w <= 0 || w > 480 || h <= 0 || h > 480) return false;
    if (ff != nullptr && w == allocW && h == allocH) return true;
    release();
    allocW = w;
    allocH = h;
    planeStride = (w + 7) & ~7;
    ff = static_cast<Firefly *>(alloc(FF_MAX * sizeof(Firefly)));
    rings = static_cast<Ring *>(alloc(RING_CAP * sizeof(Ring)));
    rowCross = static_cast<uint8_t *>(alloc(static_cast<size_t>(h) * RING_CAP));
    draws = static_cast<FfDraw *>(allocHot(FF_MAX * sizeof(FfDraw)));
    planes = static_cast<uint16_t *>(allocHot(3 * static_cast<size_t>(planeStride) * sizeof(uint16_t)));
    runs = static_cast<Run *>(allocHot(MAX_RUNS * sizeof(Run)));
    ringChan = static_cast<uint8_t *>(allocHot(RING_CAP));
    ringSign = static_cast<int8_t *>(allocHot(RING_CAP));
    crossM = static_cast<uint8_t *>(allocHot(RING_CAP));
    crossK = static_cast<uint8_t *>(allocHot(RING_CAP));
    spans = static_cast<int16_t *>(allocHot(FF_MAX * 2 * sizeof(int16_t)));
    if (ff == nullptr || rings == nullptr || rowCross == nullptr || draws == nullptr || planes == nullptr ||
        runs == nullptr || ringChan == nullptr || ringSign == nullptr || crossM == nullptr || crossK == nullptr ||
        spans == nullptr) {
        // A half finished init() leaves the slab as it found it (gm-bzu.15).
        release();
        return false;
    }
    // The page's init: the centre, the radius and a fresh generator. The
    // swarm itself is built by the first frame, which knows the count.
    cx = w / 2.0;
    cy = h / 2.0;
    rMax = (w < h ? w : h) * 0.46;
    xl0 = w / 2;
    xr0 = (w + 1) / 2;
    fracX = (w & 1) ? 0.5 : 0.0;
    rng = 0x9e3779b9u;
    builtCount = -1;
    lastThemeGen = 0xFFFFFFFFu;
    ffCount = 0;
    return true;
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[BG_ANIM_PARAMS]) {
    const int count = static_cast<int>(floor(15 + (p[1] / 100.0) * 25 + 0.5));
    bool rebuild = false;
    if (count != builtCount) {
        buildSwarm(count);
        rebuild = true;
    }
    if (rebuild || themeGen() != lastThemeGen) {
        rebuildTheme();
        lastThemeGen = themeGen();
    }
    ffCount = count;
    // Speed calibration (gm-33fm): 29/64 on the page's speed curve, so the
    // half change time lands on the fleet's 1200 ms at Speed 50.
    const double speed = pow(2.0, (static_cast<int>(p[0]) - 50) / 18.2) * (29.0 / 64);
    const double glow = 0.7 + (p[2] / 100.0) * 0.8;
    const double shimAmt = p[3] / 100.0;
    const double spread = pmul(p[4], 0.35, 1.35); // swarm radius about the centre
    const double drift = pmul(p[5], 0, 2);        // wander amplitude
    halo = 2 * pmul(p[7], 0.3, 2.3);              // glow falloff exponent
    halo2 = halo == 2.0;
    haloF = static_cast<float>(halo);
    double pulseDepth = 0.72, pulseBase = 0.28;
    if (p[6] != 50) {
        pulseDepth = p[6] < 50 ? 0.72 * (p[6] / 50.0) : 0.72 + ((p[6] - 50) / 50.0) * 0.28;
        pulseBase = 1 - pulseDepth;
    }
    const double shimPeriod = 14000 - shimAmt * 8000;
    const double t = tMs * speed;
    const double ringPos = fmod(t, shimPeriod) / shimPeriod;
    for (int i = 0; i < ffCount; i++) {
        const Firefly &f = ff[i];
        FfDraw &d = draws[i];
        const double hx = cx + (f.homeX - cx) * spread;
        const double hy = cy + (f.homeY - cy) * spread;
        const double x = hx + f.ax1 * drift * sin(f.wx1 * t + f.px1) + f.ax2 * drift * sin(f.wx2 * t + f.px2);
        const double y = hy + f.ay1 * drift * sin(f.wy1 * t + f.py1) + f.ay2 * drift * sin(f.wy2 * t + f.py2);
        double s = sin(f.pulseFreq * t + f.pulsePhase);
        if (s < 0) s = 0;
        const double pulse = pow(s, 2.2);
        double brightness = pulseBase + pulseDepth * pulse;
        if (shimAmt > 0) {
            const double dr = f.radialNorm - ringPos;
            brightness += shimAmt * exp(-(dr * dr) / (2 * 0.09 * 0.09));
        }
        if (brightness > 1.3) brightness = 1.3;
        const double R = (6 + f.size * 8) * glow;
        d.x = x;
        d.y = y;
        d.R = R;
        d.brightness = brightness;
        const double xi = floor(x), yi = floor(y);
        d.xInt = static_cast<int16_t>(xi);
        d.yInt = static_cast<int16_t>(yi);
        d.xFrac = static_cast<float>(x - xi);
        d.yFrac = static_cast<float>(y - yi);
        d.invR = static_cast<float>(1.0 / R);
        d.brF = static_cast<float>(brightness);
        double v = floor(x - R);
        d.x0 = static_cast<int16_t>(v < 0 ? 0 : v);
        v = ceil(x + R);
        d.x1 = static_cast<int16_t>(v > w - 1 ? w - 1 : v);
        v = floor(y - R);
        d.y0 = static_cast<int16_t>(v < 0 ? 0 : v);
        v = ceil(y + R);
        d.y1 = static_cast<int16_t>(v > h - 1 ? h - 1 : v);
    }
}

// The background of one row as runs in ascending x. The ring crossings of
// the row (one byte each, NO_CROSS for a ring outside it) are sorted by
// column, the colour steps once per crossing, and both halves of the row
// mirror the same list about the centre.
int buildRuns(int y, int w) {
    const uint8_t *row = rowCross + static_cast<size_t>(y) * RING_CAP;
    int n = 0;
    for (int k = 0; k < ringN; k++) {
        const int m = row[k];
        if (m == NO_CROSS) continue;
        int j = n - 1;
        while (j >= 0 && crossM[j] > m) {
            crossM[j + 1] = crossM[j];
            crossK[j + 1] = crossK[j];
            j--;
        }
        crossM[j + 1] = static_cast<uint8_t>(m);
        crossK[j + 1] = static_cast<uint8_t>(k);
        n++;
    }
    // Colour at each level: level i is the centre colour plus the first i
    // crossings' steps. Left half first, from the outermost level in.
    int col[3] = {c0[0], c0[1], c0[2]};
    for (int i = 0; i < n; i++) col[ringChan[crossK[i]]] += ringSign[crossK[i]];
    int nr = 0;
    auto emit = [&](int x0, int x1, const int *c) {
        if (x0 < 0) x0 = 0;
        if (x1 > w - 1) x1 = w - 1;
        if (nr > 0 && x0 <= runs[nr - 1].x1) x0 = runs[nr - 1].x1 + 1;
        if (x0 > x1 || nr >= MAX_RUNS) return;
        Run &r = runs[nr++];
        r.x0 = static_cast<int16_t>(x0);
        r.x1 = static_cast<int16_t>(x1);
        r.r = static_cast<uint8_t>(c[0]);
        r.g = static_cast<uint8_t>(c[1]);
        r.b = static_cast<uint8_t>(c[2]);
        r.c565 = rgb565(r.r, r.g, r.b);
    };
    for (int i = n; i >= 0; i--) {
        const int mLo = i == 0 ? 0 : crossM[i - 1];
        const int x0 = i == n ? 0 : xl0 - crossM[i] + 1;
        emit(x0, xl0 - mLo, col);
        if (i > 0) col[ringChan[crossK[i - 1]]] -= ringSign[crossK[i - 1]];
    }
    for (int i = 0; i <= n; i++) {
        if (i > 0) col[ringChan[crossK[i - 1]]] += ringSign[crossK[i - 1]];
        const int mLo = i == 0 ? 0 : crossM[i - 1];
        const int x1 = i == n ? w - 1 : xr0 + crossM[i] - 1;
        // The last run has to reach the right edge, so its start is clamped
        // into the row. The left half heals itself, because a start left of
        // column 0 is clamped up and the next iteration carries on from
        // there; the right half has no iteration after this one. A ring
        // whose crossing sits at the edge (m = 240 at w = 480) put this
        // start at column 480, the run was dropped as empty, and the row
        // then ended at the crossing before it. Two things went wrong there
        // and the fuzz found both: the columns past it kept the previous
        // frame's pixels, and the span walk in renderRow ran off the end of
        // the run list looking for them (gm-pciz; ASan, whole fleet run,
        // AnimFireflies.cpp:614). The colour is the one this iteration
        // holds, the outermost level, which is what the page's gradient has
        // past the last ring.
        int x0 = xr0 + mLo;
        if (i == n && x0 > w - 1) x0 = w - 1;
        emit(x0, x1, col);
    }
    return nr;
}

// Broadcast-fill one row span with a single RGB565 colour, eight pixels per
// PIE store. dst must be 16-byte aligned; the caller pays a scalar prefix.
#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
__attribute__((noinline)) static void fillRowPie(uint16_t *__restrict dstIn, uint16_t color, int nOct) {
    alignas(16) static uint16_t bcast[8];
    for (int i = 0; i < 8; i++) {
        bcast[i] = color;
    }
    const uint16_t *src = bcast;
    uint16_t *wr = dstIn;
    asm volatile("ee.vld.128.ip q0, %[src], 0\n" // q0 = color x8, resident for the loop
                 "loopnez %[n], 2f\n"
                 "ee.vst.128.ip q0, %[wr], 16\n"
                 "2:\n"
                 : [wr] "+r"(wr)
                 : [src] "r"(src), [n] "r"(nOct)
                 : "memory");
}
#endif

template <bool Asm> BGANIM_INLINE void fillSpan(uint16_t *dst, uint16_t c, int n) {
#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
    if (Asm && n >= 16) {
        const int pre = static_cast<int>((-reinterpret_cast<uintptr_t>(dst)) & 15u) >> 1;
        for (int i = 0; i < pre; i++) dst[i] = c;
        const int groups = (n - pre) >> 3;
        fillRowPie(dst + pre, c, groups);
        for (int i = pre + (groups << 3); i < n; i++) dst[i] = c;
        return;
    }
#endif
    for (int i = 0; i < n; i++) dst[i] = c;
}

// A fast inverse square root: the classic bit estimate and two Newton
// steps, which leaves a few ulp of float error. The sprite loop's tie
// windows are sized for that error; sqrtf is a library call here.
BGANIM_INLINE float rsqrtF(float x) {
    union {
        float f;
        uint32_t u;
    } v;
    v.f = x;
    v.u = 0x5f3759dfu - (v.u >> 1);
    float y = v.f;
    y = y * (1.5f - 0.5f * x * y * y);
    y = y * (1.5f - 0.5f * x * y * y);
    return y;
}

// The page's per-pixel sprite expression in double, for a pixel the float
// path could not settle. Writes the three planes and returns.
void spritePixelDouble(const FfDraw &d, int xx, int yy, uint16_t *pr, uint16_t *pg, uint16_t *pb) {
    const double dx = xx - d.x, dy = yy - d.y;
    const double dist = sqrt(dx * dx + dy * dy);
    if (dist > d.R) return;
    const double norm = dist / d.R;
    const double falloff = halo2 ? (1 - norm) * (1 - norm) : pow(1 - norm, halo);
    double core = 1 - norm * 2.6;
    if (core < 0) core = 0;
    const double a = falloff * d.brightness;
    if (a <= 0.004) return;
    const double cr = coreC[0] * core + d.gr * (1 - core);
    const double cg = coreC[1] * core + d.gg * (1 - core);
    const double cb = coreC[2] * core + d.gb * (1 - core);
    double v = pr[xx] + cr * a;
    pr[xx] = static_cast<uint16_t>(clampRound(v > 255 ? 255 : v));
    v = pg[xx] + cg * a;
    pg[xx] = static_cast<uint16_t>(clampRound(v > 255 ? 255 : v));
    v = pb[xx] + cb * a;
    pb[xx] = static_cast<uint16_t>(clampRound(v > 255 ? 255 : v));
}

// One sprite over one row of the planes. Float, with the double fallback
// at every discontinuity: the rim, the alpha cutoff and the half-unit
// rounding boundary of each channel. The tie windows (RIM, CUT, HALF) are
// above the float error by a margin of about three.
constexpr float RIM = 1e-5f;
constexpr float CUT = 1e-6f;
constexpr float HALF = 2e-3f;

// Round half to even in float, used only where HALF has ruled out a tie.
// Both of these are only ever given a plane byte plus a non-negative
// contribution, so floor is truncation and the FPU's own convert
// instructions do it. Written with floorf they were a library call each,
// six a sprite pixel, about 130,000 a frame on the default parameters; the
// device toolchain emits a real call8 to floorf. Same bits either way for a
// non-negative value under 2^24.
BGANIM_INLINE float truncPos(float v) { return static_cast<float>(static_cast<int>(v)); }
BGANIM_INLINE int roundF(float v) { return static_cast<int>(v + 0.5f); }

void spriteRow(const FfDraw &d, int yy, uint16_t *pr, uint16_t *pg, uint16_t *pb) {
    const float dyf = static_cast<float>(yy - d.yInt) - d.yFrac;
    const float dy2 = dyf * dyf;
    const float cr0 = coreC[0], cg0 = coreC[1], cb0 = coreC[2];
    for (int xx = d.x0; xx <= d.x1; xx++) {
        const float dxf = static_cast<float>(xx - d.xInt) - d.xFrac;
        const float d2 = dxf * dxf + dy2;
        const float dist = d2 > 0 ? d2 * rsqrtF(d2) : 0.0f;
        const float norm = dist * d.invR;
        if (norm > 1.0f + RIM) continue;
        if (norm > 1.0f - RIM) {
            spritePixelDouble(d, xx, yy, pr, pg, pb);
            continue;
        }
        const float u = 1.0f - norm;
        const float falloff = halo2 ? u * u : powf(u, haloF);
        const float a = falloff * d.brF;
        if (a < 0.004f - CUT) continue;
        if (a < 0.004f + CUT) {
            spritePixelDouble(d, xx, yy, pr, pg, pb);
            continue;
        }
        float core = 1.0f - norm * 2.6f;
        if (core < 0) core = 0;
        const float ic = 1.0f - core;
        const float vr = pr[xx] + (cr0 * core + d.gr * ic) * a;
        const float vg = pg[xx] + (cg0 * core + d.gg * ic) * a;
        const float vb = pb[xx] + (cb0 * core + d.gb * ic) * a;
        const float fr = vr - truncPos(vr), fg = vg - truncPos(vg), fb = vb - truncPos(vb);
        if (fabsf(fr - 0.5f) < HALF || fabsf(fg - 0.5f) < HALF || fabsf(fb - 0.5f) < HALF) {
            spritePixelDouble(d, xx, yy, pr, pg, pb);
            continue;
        }
        pr[xx] = static_cast<uint16_t>(vr >= 255.0f ? 255 : roundF(vr));
        pg[xx] = static_cast<uint16_t>(vg >= 255.0f ? 255 : roundF(vg));
        pb[xx] = static_cast<uint16_t>(vb >= 255.0f ? 255 : roundF(vb));
    }
}

template <bool Asm> void renderRow(uint16_t *out, int y, int w) {
    const int nr = buildRuns(y, w);
    for (int i = 0; i < nr; i++) fillSpan<Asm>(out + runs[i].x0, runs[i].c565, runs[i].x1 - runs[i].x0 + 1);

    // The sprites on this row, and the union of their spans, sorted.
    int ns = 0;
    for (int i = 0; i < ffCount; i++) {
        const FfDraw &d = draws[i];
        if (y < d.y0 || y > d.y1) continue;
        int j = ns - 1;
        while (j >= 0 && spans[2 * j] > d.x0) {
            spans[2 * j + 2] = spans[2 * j];
            spans[2 * j + 3] = spans[2 * j + 1];
            j--;
        }
        spans[2 * j + 2] = d.x0;
        spans[2 * j + 3] = d.x1;
        ns++;
    }
    if (ns == 0) return;
    int nm = 0;
    for (int i = 0; i < ns; i++) {
        if (nm > 0 && spans[2 * i] <= spans[2 * nm - 1] + 1) {
            if (spans[2 * i + 1] > spans[2 * nm - 1]) spans[2 * nm - 1] = spans[2 * i + 1];
        } else {
            spans[2 * nm] = spans[2 * i];
            spans[2 * nm + 1] = spans[2 * i + 1];
            nm++;
        }
    }
    // The background under the spans, in 8 bits, from the runs.
    uint16_t *pr = planes;
    uint16_t *pg = pr + planeStride;
    uint16_t *pb = pg + planeStride;
    int ri = 0;
    for (int i = 0; i < nm; i++) {
        int x = spans[2 * i];
        const int xe = spans[2 * i + 1];
        while (ri > 0 && runs[ri].x0 > x) ri--;
        // The runs cover the whole row, so these bounds never stop the walk
        // early. They are here so that a future change to buildRuns cannot
        // turn a gap in the coverage into a read past the run list.
        while (ri + 1 < nr && runs[ri].x1 < x) ri++;
        while (x <= xe) {
            const Run &r = runs[ri];
            const int stop = r.x1 < xe ? r.x1 : xe;
            for (; x <= stop; x++) {
                pr[x] = r.r;
                pg[x] = r.g;
                pb[x] = r.b;
            }
            if (x > xe) break;
            if (ri + 1 >= nr) break;
            ri++;
        }
    }
    // The sprites in index order, which is the order the page chains them.
    for (int i = 0; i < ffCount; i++) {
        const FfDraw &d = draws[i];
        if (y < d.y0 || y > d.y1) continue;
        spriteRow(d, y, pr, pg, pb);
    }
    // Pack the spans: (r >> 3, g >> 2, b >> 3), the quantisation the page's
    // check applies.
    for (int i = 0; i < nm; i++) {
        for (int x = spans[2 * i]; x <= spans[2 * i + 1]; x++) {
            out[x] = static_cast<uint16_t>(((pr[x] >> 3) << 11) | ((pg[x] >> 2) << 5) | (pb[x] >> 3));
        }
    }
}

// The spec. Host bench goldens run against this, and the device equivalence
// test (SleepAnimation::runAnimTest, /api/debug/animtest) checks band()
// against it pixel for pixel. The two differ only in the run fill.
void bandRef(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int r = 0; r < rows; r++) renderRow<false>(dst + static_cast<size_t>(r) * w, y0 + r, w);
}

void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int r = 0; r < rows; r++) renderRow<true>(dst + static_cast<size_t>(r) * w, y0 + r, w);
}

void release() {
    releaseTable(ff, static_cast<size_t>(FF_MAX) * sizeof(Firefly));
    releaseTable(rings, static_cast<size_t>(RING_CAP) * sizeof(Ring));
    releaseTable(rowCross, static_cast<size_t>(allocH) * RING_CAP);
    releaseTable(draws, static_cast<size_t>(FF_MAX) * sizeof(FfDraw));
    releaseTable(planes, 3 * static_cast<size_t>(planeStride) * sizeof(uint16_t));
    releaseTable(runs, static_cast<size_t>(MAX_RUNS) * sizeof(Run));
    releaseTable(ringChan, RING_CAP);
    releaseTable(ringSign, RING_CAP);
    releaseTable(crossM, RING_CAP);
    releaseTable(crossK, RING_CAP);
    releaseTable(spans, static_cast<size_t>(FF_MAX) * 2 * sizeof(int16_t));
    allocW = allocH = planeStride = 0;
    ffCount = 0;
    builtCount = -1;
    ringN = 0;
    lastThemeGen = 0xFFFFFFFFu;
}

} // namespace

extern const BgAnimation bg_anim_fireflies;
const BgAnimation bg_anim_fireflies = {
    "fireflies",
    "Fireflies",
    {{"speed", "Speed", 50},
     {"count", "Count", 60},
     {"glow", "Glow", 55},
     {"shimmer", "Shimmer", 40},
     {"spread", "Spread", 50},
     {"drift", "Drift", 50},
     {"pulse", "Pulse depth", 50},
     {"halo", "Halo", 50}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
