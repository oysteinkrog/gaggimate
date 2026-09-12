#ifndef GAGGIMATE_SIM

// "Steam": entry 10 of tools/animbench/web/anim_bench.html, the page the
// owner approved the look on. Rising wisps built from chains of overlapping
// soft blobs; each blob's position, radius and alpha is a pure function of
// its age (analytic, no feedback buffer), with a 4L(1-L) lifecycle envelope
// so nothing pops. Design: anim-particles (Fable), 2026-08-15.
//
// This file draws the page's picture, pixel for pixel after RGB565
// quantisation (gm-pciz, 2026-09-12), the same way AnimFireflies.cpp does,
// and that file's header carries the reasoning the two share. What was
// different here before: an xorshift generator where the page seeds
// mulberry32(0x1234abcd), a wisp count of 2 + p * 3 / 100 where the page
// rounds (4 wisps at the default, not 3), a vertical background where the
// page's is radial (themeRGB(9) at the centre to themeRGB(3) at half the
// panel), whole-pixel blob centres and radii, a 64-entry table for the
// page's per-pixel pow(1 - d / R, 1.6), and a 5-6-5 additive blend where
// the page accumulates in 8 bits; 21,300 to 23,100 of the 230,400 pixels
// of each golden frame differed. What is here now:
//
// - The wisps and blobs are the page's: the same draws from
//   mulberry32(0x1234abcd) in the same order, built by the first frame at
//   that frame's time, a blob reborn past its lifetime with the page's
//   birth = t - (age % lifetime) + rand() * 40, so the generator is
//   consumed exactly as the page consumes it.
// - The background is the page's radial gradient, as rings and per-row
//   crossings found from the page's own double expression (AnimFireflies
//   explains the scheme; RING_CAP is 64 here, the fleet's themes need 4
//   to 30 rings).
// - A blob's pixels are computed in float and a pixel within the tie
//   windows of the rim, the 0.003 alpha cutoff or a half-unit rounding
//   boundary is recomputed in double in the page's expression order. The
//   page's pow(1 - d / R, 1.6) is a 4,097-entry float table over 1 - d / R
//   with linear interpolation: the table's error is under 3e-8 of a unit
//   of alpha where the cutoff lets a pixel through, so the value error it
//   contributes is under 1e-5 and the 2e-3 tie window covers it; pow
//   itself is a library call the loop cannot afford per pixel. A blob's
//   pixels past the radius where alpha falls under the cutoff are skipped
//   on the squared distance alone.
// - The per-frame constants are double with libm's sin and pow.
//
// What page_vs_golden.js still reports for this animation, and why it is
// the harness and not this file. Both sides build their wisps at the time
// of the first frame they are given, and the birth of every blob is that
// time minus a draw from the generator, so the whole picture hangs on when
// the first frame arrived. bench.cpp renders a warm-up frame at t = 0
// before the timed frames start at 1000 ms; the tool starts the page at
// 1000 ms, which its own header records as the one thing it cannot model.
// Measured both ways on frames 30, 120 and 210 (gm-pciz, 2026-09-12):
// with the page given the same warm-up frame at t = 0, 0 differing pixels
// at all three; without it, 14,742, 16,162 and 16,724 of 230,400, mean
// absolute deviation 2.17, 2.16 and 2.19 per channel. The device does not
// have a warm-up frame, so on the device this file and the page build at
// the same time and draw the same picture. Nothing here can close the gap
// the tool reports without moving away from the page's own model.
//
// The host bench band is 0.61 ms a frame against the old port's 0.16,
// all of it the per-pixel float blob work, which is the price of the
// page's model.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace {
using namespace bganim;

constexpr int WISPS_MAX = 5;
constexpr int BLOBS_PER_WISP = 11;
constexpr int BLOBS_MAX = WISPS_MAX * BLOBS_PER_WISP;
constexpr int RING_CAP = 64;    // background rings a theme may need
constexpr int NO_CROSS = 255;   // rowCross value: this ring does not reach this row
constexpr int MAX_RUNS = 2 * RING_CAP + 2;
constexpr int POW_N = 4096;     // pow(u, 1.6) table intervals over u in 0..1

struct Wisp {
    double x0, y0;
    double swayPhase1, swayPhase2, swayFreq1, swayFreq2;
};
struct Blob {
    int wisp;
    double birth, lifetime, seed;
};

// What band() reads per blob: the double constants for the fallback, the
// float copies for the fast path, the bounding box and the colour.
struct BlobDraw {
    double x, y, R, alpha;
    float xFrac, yFrac, invR, alphaF, cutD2;
    int16_t xInt, yInt, x0, x1, y0, y1;
    uint8_t r, g, b, visible;
};

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

Wisp *wisps = nullptr;       // [WISPS_MAX]         PSRAM, frame() only
Blob *blobs = nullptr;       // [BLOBS_MAX]         PSRAM, frame() only
Ring *rings = nullptr;       // [RING_CAP]          PSRAM, rebuild only
uint8_t *rowCross = nullptr; // [h * RING_CAP]      PSRAM, one row read per row
float *powTab = nullptr;     // [POW_N + 1]         PSRAM, gathered per pixel
BlobDraw *draws = nullptr;   // [BLOBS_MAX]         hot
uint16_t *planes = nullptr;  // [3 * planeStride]   hot, 8-bit channels in 16-bit lanes
Run *runs = nullptr;         // [MAX_RUNS]          hot
uint8_t *ringChan = nullptr; // [RING_CAP]          hot
int8_t *ringSign = nullptr;  // [RING_CAP]          hot
uint8_t *crossM = nullptr;   // [RING_CAP]          hot
uint8_t *crossK = nullptr;   // [RING_CAP]          hot
int16_t *spans = nullptr;    // [BLOBS_MAX * 2]     hot
uint16_t *fillBcast = nullptr; // [8]               hot, 16-byte aligned; see fillRowPie

int allocW = 0, allocH = 0, planeStride = 0;
int builtCount = -1;
int activeBlobs = 0;
int ringN = 0;
uint8_t c0[3], c1[3];
double cx = 0, cy = 0, rDisplay = 0;
int xl0 = 0, xr0 = 0;
double fracX = 0;
uint32_t rng = 0x1234abcdu;
uint32_t lastThemeGen = 0xFFFFFFFFu;

// Hot slab at w = 480: draws 55 * 72 = 3,960 B, planes 2,880, runs 130 * 8
// = 1,040, ringChan 64, ringSign 64, crossM 64, crossK 64, spans 220 (224),
// fillBcast 16: 8,376 B of the 9,216 B an animation may take. PSRAM holds
// the wisps, the blobs, the rings, the pow table (16,388 B) and rowCross
// (30,720 B at h = 480).
void release();

uint32_t mulberry(uint32_t &seed) {
    seed += 0x6D2B79F5u;
    uint32_t t = (seed ^ (seed >> 15)) * (1u | seed);
    t = (t + (t ^ (t >> 7)) * (61u | t)) ^ t;
    return t ^ (t >> 14);
}
double rand01() { return mulberry(rng) / 4294967296.0; }

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

int pageBg(int c, double dx, double dy) {
    double n = sqrt(dx * dx + dy * dy) / rDisplay;
    if (n > 1) n = 1;
    return clampRound(c0[c] + (c1[c] - c0[c]) * n);
}

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

void rebuildTheme() {
    themeRGB(9, c0);
    themeRGB(3, c1);
    ringN = 0;
    for (int c = 0; c < 3; c++) {
        const int delta = c1[c] - c0[c];
        const int steps = delta < 0 ? -delta : delta;
        for (int j = 1; j <= steps && ringN < RING_CAP; j++) {
            Ring &rg = rings[ringN++];
            rg.T = ((j - 0.5) / steps) * rDisplay;
            rg.chan = static_cast<uint8_t>(c);
            rg.step = static_cast<uint8_t>(j);
            rg.sign = static_cast<int8_t>(delta < 0 ? -1 : 1);
        }
    }
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
}

// The page's buildWisps, at the time of the frame that builds them.
void buildWisps(int count, double tMs) {
    const double y0 = allocH * 0.90;
    const double dy = y0 - cy;
    double inside = rDisplay * rDisplay - dy * dy;
    if (inside < 0) inside = 0;
    const double halfSpan = sqrt(inside) * 0.7;
    for (int i = 0; i < count; i++) {
        Wisp &wp = wisps[i];
        wp.x0 = cx + (rand01() * 2 - 1) * halfSpan;
        wp.y0 = y0;
        wp.swayPhase1 = rand01() * M_PI * 2;
        wp.swayPhase2 = rand01() * M_PI * 2;
        wp.swayFreq1 = 0.000084375 + rand01() * 0.00005625;
        wp.swayFreq2 = 0.00024375 + rand01() * 0.00013125;
    }
    for (int i = 0; i < count; i++) {
        for (int k = 0; k < BLOBS_PER_WISP; k++) {
            Blob &b = blobs[i * BLOBS_PER_WISP + k];
            const double lifetime = 22400 + rand01() * 9600;
            b.wisp = i;
            b.birth = tMs - rand01() * lifetime;
            b.lifetime = lifetime;
            b.seed = rand01() * M_PI * 2;
        }
    }
    builtCount = count;
}

bool init(int w, int h) {
    if (w <= 0 || w > 480 || h <= 0 || h > 480) return false;
    if (blobs != nullptr && w == allocW && h == allocH) return true;
    release();
    allocW = w;
    allocH = h;
    planeStride = (w + 7) & ~7;
    wisps = static_cast<Wisp *>(alloc(WISPS_MAX * sizeof(Wisp)));
    blobs = static_cast<Blob *>(alloc(BLOBS_MAX * sizeof(Blob)));
    rings = static_cast<Ring *>(alloc(RING_CAP * sizeof(Ring)));
    rowCross = static_cast<uint8_t *>(alloc(static_cast<size_t>(h) * RING_CAP));
    powTab = static_cast<float *>(alloc((POW_N + 1) * sizeof(float)));
    draws = static_cast<BlobDraw *>(allocHot(BLOBS_MAX * sizeof(BlobDraw)));
    planes = static_cast<uint16_t *>(allocHot(3 * static_cast<size_t>(planeStride) * sizeof(uint16_t)));
    runs = static_cast<Run *>(allocHot(MAX_RUNS * sizeof(Run)));
    ringChan = static_cast<uint8_t *>(allocHot(RING_CAP));
    ringSign = static_cast<int8_t *>(allocHot(RING_CAP));
    crossM = static_cast<uint8_t *>(allocHot(RING_CAP));
    crossK = static_cast<uint8_t *>(allocHot(RING_CAP));
    spans = static_cast<int16_t *>(allocHot(BLOBS_MAX * 2 * sizeof(int16_t)));
    fillBcast = static_cast<uint16_t *>(allocHot(8 * sizeof(uint16_t)));
    if (wisps == nullptr || blobs == nullptr || rings == nullptr || rowCross == nullptr || powTab == nullptr ||
        draws == nullptr || planes == nullptr || runs == nullptr || ringChan == nullptr || ringSign == nullptr ||
        crossM == nullptr || crossK == nullptr || spans == nullptr || fillBcast == nullptr) {
        // A half finished init() leaves the slab as it found it (gm-bzu.15).
        release();
        return false;
    }
    cx = w / 2.0;
    cy = h / 2.0;
    rDisplay = (w < h ? w : h) / 2.0;
    xl0 = w / 2;
    xr0 = (w + 1) / 2;
    fracX = (w & 1) ? 0.5 : 0.0;
    for (int i = 0; i <= POW_N; i++) powTab[i] = static_cast<float>(pow(i / static_cast<double>(POW_N), 1.6));
    rng = 0x1234abcdu;
    builtCount = -1;
    activeBlobs = 0;
    lastThemeGen = 0xFFFFFFFFu;
    return true;
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[BG_ANIM_PARAMS]) {
    if (themeGen() != lastThemeGen) {
        rebuildTheme();
        lastThemeGen = themeGen();
    }
    const double t = tMs;
    const int count = static_cast<int>(floor(2 + (p[1] / 100.0) * 3 + 0.5));
    if (count != builtCount) buildWisps(count, t);
    // Speed calibration (gm-33fm): the rise, the puff lifetime and the sway
    // frequencies are all scaled by 3/16 together; the page carries the
    // same constants.
    const double riseSpeed = 0.006375 * pow(2.0, (static_cast<int>(p[0]) - 50) / 18.2);
    const double swirl = 0.5 + (p[2] / 100.0) * 1.7;
    const double density = 0.5 + (p[3] / 100.0) * 0.8;
    const double sizeT = (static_cast<int>(p[4]) - 50) / 50.0, sizeMul = 1 + sizeT * (sizeT < 0 ? 0.5 : 0.8);
    const double spreadT = (static_cast<int>(p[5]) - 50) / 50.0,
                 spreadMul = 1 + spreadT * (spreadT < 0 ? 0.85 : 0.5);
    const int tintBase = 200 + ((static_cast<int>(p[6]) - 50) * 11) / 5; // Math.trunc, and C's / truncates too
    const double taperT = (static_cast<int>(p[7]) - 50) / 50.0, taper = 0.3 + taperT * (taperT < 0 ? 0.3 : 0.7);
    const double maxHeight = h * 0.62;
    activeBlobs = count * BLOBS_PER_WISP;
    for (int i = 0; i < activeBlobs; i++) {
        Blob &b = blobs[i];
        BlobDraw &d = draws[i];
        d.visible = 0;
        double age = t - b.birth;
        if (age > b.lifetime) {
            b.birth = t - fmod(age, b.lifetime) + rand01() * 40;
            age = t - b.birth;
        }
        double L = age / b.lifetime;
        if (L > 1) L = 1;
        if (L < 0) L = 0;
        const Wisp &wp = wisps[b.wisp];
        const double rise = riseSpeed * age;
        double heightFrac = rise / maxHeight;
        if (heightFrac > 1) heightFrac = 1;
        const double y = wp.y0 - rise;
        if (y < -30) continue;
        const double swayAmp = (5 + 22 * heightFrac) * swirl;
        const double wx0 = cx + (wp.x0 - cx) * spreadMul;
        const double x = wx0 + swayAmp * sin(wp.swayFreq1 * t + wp.swayPhase1 + b.seed) +
                         swayAmp * 0.35 * sin(wp.swayFreq2 * t + wp.swayPhase2 + b.seed * 1.7);
        double alpha = density * 0.44 * 4 * L * (1 - L) * (1 - heightFrac * taper);
        if (alpha > 0.4) alpha = 0.4;
        if (alpha <= 0.003) continue;
        const double R = (10 + 26 * heightFrac) * (0.85 + 0.3 * sin(b.seed)) * sizeMul;
        uint8_t c[3];
        themeRGB(tintBase + static_cast<int>(heightFrac * 55), c);
        d.r = c[0];
        d.g = c[1];
        d.b = c[2];
        d.x = x;
        d.y = y;
        d.R = R;
        d.alpha = alpha;
        const double xi = floor(x), yi = floor(y);
        d.xInt = static_cast<int16_t>(xi);
        d.yInt = static_cast<int16_t>(yi);
        d.xFrac = static_cast<float>(x - xi);
        d.yFrac = static_cast<float>(y - yi);
        d.invR = static_cast<float>(1.0 / R);
        d.alphaF = static_cast<float>(alpha);
        // Past this squared distance alpha is below the cutoff by more than
        // the tie window, so the pixel loop skips it without a square root:
        // the cutoff sits at u = (0.003 / alpha)^(1 / 1.6), and 1e-3 of
        // normalised distance beyond it moves alpha by at least 5e-6.
        double normCut = 1 - pow(0.003 / alpha, 1 / 1.6) + 1e-3;
        if (normCut > 1.0001) normCut = 1.0001;
        d.cutD2 = static_cast<float>(normCut * R * normCut * R);
        double v = floor(x - R);
        d.x0 = static_cast<int16_t>(v < 0 ? 0 : v);
        v = ceil(x + R);
        d.x1 = static_cast<int16_t>(v > w - 1 ? w - 1 : v);
        v = floor(y - R);
        d.y0 = static_cast<int16_t>(v < 0 ? 0 : v);
        v = ceil(y + R);
        d.y1 = static_cast<int16_t>(v > h - 1 ? h - 1 : v);
        d.visible = 1;
    }
}

// The background of one row as runs in ascending x (AnimFireflies.cpp).
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
        // The last run has to reach the right edge; see the same clamp in
        // AnimFireflies.cpp, which carries the reasoning and the fuzz case.
        int x0 = xr0 + mLo;
        if (i == n && x0 > w - 1) x0 = w - 1;
        emit(x0, x1, col);
    }
    return nr;
}

#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
// Eight pixels of solid fill colour per store, zero-overhead hardware loop.
// `bc` must point at a 16-byte-aligned buffer holding the fill colour
// repeated eight times; `wr` must be 16-byte aligned (EE.VST.128.IP masks
// the low four address bits silently instead of trapping); w8 must be at
// least 1, because `loop` with a zero trip count wraps LCOUNT. The caller
// pays a scalar prefix and tail and never calls this with zero groups.
__attribute__((noinline)) static void fillRowPie(uint16_t *__restrict wr, const uint16_t *__restrict bc, int w8) {
    uint16_t *dst = wr;
    const uint16_t *bcp = bc;
    int n = w8;
    asm volatile("ee.vld.128.ip q0, %[bc], 0\n" // eight copies of the fill colour, loaded once
                 "loop %[n], 1f\n"
                 "ee.vst.128.ip q0, %[dst], 16\n"
                 "1:\n"
                 : [dst] "+r"(dst), [n] "+r"(n)
                 : [bc] "r"(bcp)
                 : "memory");
}
#endif

template <bool Asm> BGANIM_INLINE void fillSpan(uint16_t *dst, uint16_t c, int n) {
#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
    if (Asm && n >= 16) {
        const int pre = static_cast<int>((-reinterpret_cast<uintptr_t>(dst)) & 15u) >> 1;
        for (int i = 0; i < pre; i++) dst[i] = c;
        const int groups = (n - pre) >> 3;
        for (int k = 0; k < 8; k++) fillBcast[k] = c;
        fillRowPie(dst + pre, fillBcast, groups);
        for (int i = pre + (groups << 3); i < n; i++) dst[i] = c;
        return;
    }
#endif
    for (int i = 0; i < n; i++) dst[i] = c;
}

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

// The page's per-pixel blob expression in double, for a pixel the float
// path could not settle.
void blobPixelDouble(const BlobDraw &d, int xx, int yy, uint16_t *pr, uint16_t *pg, uint16_t *pb) {
    const double ddx = xx - d.x, ddy = yy - d.y;
    const double dist = sqrt(ddx * ddx + ddy * ddy);
    if (dist > d.R) return;
    const double a = pow(1 - dist / d.R, 1.6) * d.alpha;
    if (a <= 0.003) return;
    double v = pr[xx] + d.r * a;
    pr[xx] = static_cast<uint16_t>(clampRound(v > 255 ? 255 : v));
    v = pg[xx] + d.g * a;
    pg[xx] = static_cast<uint16_t>(clampRound(v > 255 ? 255 : v));
    v = pb[xx] + d.b * a;
    pb[xx] = static_cast<uint16_t>(clampRound(v > 255 ? 255 : v));
}

constexpr float RIM = 1e-5f;
constexpr float CUT = 1e-6f;
constexpr float HALF = 2e-3f;

// Both of these are only ever given a plane byte plus a non-negative
// contribution, so floor is truncation and the FPU's own convert
// instructions do it. Written with floorf they were a library call each,
// six a blob pixel, about 200,000 a frame on the default parameters; the
// device toolchain emits a real call8 to floorf (xtensa-asm14.sh AnimSteam
// lists it under blobRow), and that alone was 33 ms a frame of estimated
// soft-float on the bench's weighting. Same bits either way for a
// non-negative value under 2^24.
BGANIM_INLINE float truncPos(float v) { return static_cast<float>(static_cast<int>(v)); }
BGANIM_INLINE int roundF(float v) { return static_cast<int>(v + 0.5f); }

// One blob over one row of the planes. Float, with the double fallback at
// every discontinuity: the rim, the alpha cutoff and the half-unit
// rounding boundary of each channel.
void blobRow(const BlobDraw &d, int yy, uint16_t *pr, uint16_t *pg, uint16_t *pb) {
    const float dyf = static_cast<float>(yy - d.yInt) - d.yFrac;
    const float dy2 = dyf * dyf;
    const float cr = d.r, cg = d.g, cb = d.b;
    for (int xx = d.x0; xx <= d.x1; xx++) {
        const float dxf = static_cast<float>(xx - d.xInt) - d.xFrac;
        const float d2 = dxf * dxf + dy2;
        if (d2 > d.cutD2) continue;
        const float dist = d2 > 0 ? d2 * rsqrtF(d2) : 0.0f;
        const float norm = dist * d.invR;
        if (norm > 1.0f + RIM) continue;
        if (norm > 1.0f - RIM) {
            blobPixelDouble(d, xx, yy, pr, pg, pb);
            continue;
        }
        const float u = (1.0f - norm) * static_cast<float>(POW_N);
        const int ui = static_cast<int>(u);
        const float f0 = powTab[ui];
        const float a = (f0 + (powTab[ui + 1] - f0) * (u - ui)) * d.alphaF;
        if (a < 0.003f - CUT) continue;
        if (a < 0.003f + CUT) {
            blobPixelDouble(d, xx, yy, pr, pg, pb);
            continue;
        }
        const float vr = pr[xx] + cr * a;
        const float vg = pg[xx] + cg * a;
        const float vb = pb[xx] + cb * a;
        const float fr = vr - truncPos(vr), fg = vg - truncPos(vg), fb = vb - truncPos(vb);
        if (fabsf(fr - 0.5f) < HALF || fabsf(fg - 0.5f) < HALF || fabsf(fb - 0.5f) < HALF) {
            blobPixelDouble(d, xx, yy, pr, pg, pb);
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

    int ns = 0;
    for (int i = 0; i < activeBlobs; i++) {
        const BlobDraw &d = draws[i];
        if (!d.visible || y < d.y0 || y > d.y1) continue;
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
    uint16_t *pr = planes;
    uint16_t *pg = pr + planeStride;
    uint16_t *pb = pg + planeStride;
    int ri = 0;
    for (int i = 0; i < nm; i++) {
        int x = spans[2 * i];
        const int xe = spans[2 * i + 1];
        while (ri > 0 && runs[ri].x0 > x) ri--;
        // Bounded for the same reason as in AnimFireflies.cpp: the runs
        // cover the row, and these guards keep a future gap from reading
        // past the list.
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
    // Blobs in array order, which is the order the page chains them.
    for (int i = 0; i < activeBlobs; i++) {
        const BlobDraw &d = draws[i];
        if (!d.visible || y < d.y0 || y > d.y1) continue;
        blobRow(d, y, pr, pg, pb);
    }
    for (int i = 0; i < nm; i++) {
        for (int x = spans[2 * i]; x <= spans[2 * i + 1]; x++) {
            out[x] = static_cast<uint16_t>(((pr[x] >> 3) << 11) | ((pg[x] >> 2) << 5) | (pb[x] >> 3));
        }
    }
}

// Portable reference: same output as the Xtensa-dispatched band() below,
// pixel for pixel. The host bench builds this and SleepAnimation::runAnimTest
// compares the device band() against it; the two differ only in the run
// fill.
void bandPortable(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int r = 0; r < rows; r++) renderRow<false>(dst + static_cast<size_t>(r) * w, y0 + r, w);
}

void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int r = 0; r < rows; r++) renderRow<true>(dst + static_cast<size_t>(r) * w, y0 + r, w);
}

void release() {
    releaseTable(wisps, static_cast<size_t>(WISPS_MAX) * sizeof(Wisp));
    releaseTable(blobs, static_cast<size_t>(BLOBS_MAX) * sizeof(Blob));
    releaseTable(rings, static_cast<size_t>(RING_CAP) * sizeof(Ring));
    releaseTable(rowCross, static_cast<size_t>(allocH) * RING_CAP);
    releaseTable(powTab, static_cast<size_t>(POW_N + 1) * sizeof(float));
    releaseTable(draws, static_cast<size_t>(BLOBS_MAX) * sizeof(BlobDraw));
    releaseTable(planes, 3 * static_cast<size_t>(planeStride) * sizeof(uint16_t));
    releaseTable(runs, static_cast<size_t>(MAX_RUNS) * sizeof(Run));
    releaseTable(ringChan, RING_CAP);
    releaseTable(ringSign, RING_CAP);
    releaseTable(crossM, RING_CAP);
    releaseTable(crossK, RING_CAP);
    releaseTable(spans, static_cast<size_t>(BLOBS_MAX) * 2 * sizeof(int16_t));
    releaseTable(fillBcast, 8 * sizeof(uint16_t));
    allocW = allocH = planeStride = 0;
    builtCount = -1;
    activeBlobs = 0;
    ringN = 0;
    lastThemeGen = 0xFFFFFFFFu;
}

} // namespace

extern const BgAnimation bg_anim_steam;
const BgAnimation bg_anim_steam = {
    "steam",
    "Steam",
    {{"speed", "Rise speed", 50},
     {"count", "Wisps", 55},
     {"swirl", "Swirl", 45},
     {"density", "Density", 50},
     {"size", "Puff size", 50},
     {"spread", "Base spread", 50},
     {"tint", "Steam tint", 50},
     {"taper", "Top fade", 50}},
    init,
    frame,
    band,
    release,
    bandPortable,
};

#endif // GAGGIMATE_SIM
