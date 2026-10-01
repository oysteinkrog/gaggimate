#ifndef GAGGIMATE_SIM

// "Orbits": entry 8 of tools/animbench/web/anim_bench.html, the page the
// owner approved the look on. Three to six faint elliptical paths with
// glowing bodies at golden-ratio periods and analytic (recomputed, not
// feedback) fading trails. Design: anim-geometric (Fable), 2026-08-15.
//
// This file draws the page's picture, pixel for pixel after RGB565
// quantisation (gm-pciz, 2026-09-13), the same way AnimFireflies.cpp and
// AnimSteam.cpp do, and those two headers carry the reasoning the three
// share. What was different here before, and what 5,535 of the 230,400
// pixels of the worst golden frame were made of:
//
// - The ring count was truncated where the page rounds, so the default
//   slider of 55 drew four rings against the page's five. The count also
//   sets every ring's radius, through the 0.62 / (count - 1) step, so one
//   ring too few put all of them in the wrong place.
// - A path sample was one opaque pixel at the truncated coordinate. The
//   page spreads the same 0.11 of alpha over the four pixels around the
//   float coordinate by bilinear weight and lets successive samples
//   accumulate, so the device drew a ring about four times as hard as the
//   design and up to a pixel off it.
// - The bodies and trail dots used a table cosine for the centre, the pixel
//   centre where the page measures from the pixel corner, an alpha-max
//   distance approximation with about 4% of error where the page takes a
//   square root, f squared where the page takes pow(f, 1.6), a trail count
//   truncated where the page rounds, and a 5-6-5 blend where the page
//   blends in 8 bits.
//
// What is here now:
//
// - The path layer is built once per parameter or theme change, in double,
//   by replaying the page's own accumulation: for each ring in turn, each
//   of its 480 samples in turn, the four bilinear contributions in the
//   page's order, each one stored back the way a Uint8ClampedArray stores
//   it (clamp, round half to even). The result is a list of 8-bit pixels
//   indexed by row, so a band call writes one store per path pixel and
//   pays none of that arithmetic.
// - The bodies and trail dots are the page's: centres from libm's sin and
//   cos in double, the page's radius, alpha and pow(f, 1.6), and the
//   page's per-pixel distance, coverage and 8-bit blend. A pixel is
//   computed in float and recomputed in double when any channel lands
//   within 2e-3 of a rounding boundary, which is AnimFireflies.cpp's
//   scheme and about one pixel in 500.
// - A row is composited in three 8-bit planes over the pixels something
//   actually touches, and only those are packed to RGB565. The planes are
//   needed because the page accumulates in 8 bits and the band buffer is
//   5-6-5: a blend chain in 5-6-5 does not round the same way.
// - The page's order is kept: ring 0's path, ring 0's bodies, ring 1's
//   path, and so on. It matters where a later ring's path crosses an
//   earlier ring's trail dot, which is 5 to 12 pixels a frame at the
//   default parameters.
//
// The one thing that is not the page's arithmetic. Where a path pixel
// lands on a trail dot already drawn this frame, the precomputed colour is
// not the answer, because it was composited over the background. Such a
// pixel is blended instead with the ring's combined alpha for that pixel
// (1 - the product of its samples' (1 - a), stored per pixel), which is
// the same value in exact arithmetic but rounds once where the page rounds
// once per sample. Measured over the 44 frames around each golden frame,
// the two agree at every one of those pixels; they can differ by one
// palette step on about 2% of them, so a frame that happens to cross badly
// could carry a pixel or two. Storing each pixel's samples instead would
// cost about 90 KB more and a blend chain per frame, and the page's own
// model is what the rest of this file already pays for.
//
// The path layer is indexed by exact row now, not by the 16-row bins the
// old one used, so band() no longer loses path pixels when a call straddles
// a bin boundary. The stamp lookup still bins by 16 rows, but it picks the
// bin per row rather than per call, so every band height is correct,
// including the whole-frame call that interlace_check.cpp's header records
// as broken for this animation.
//
// What drawing the page costs, so the next person does not have to measure
// it again. The host bench band is 0.076 ms a frame against the old port's
// 0.043, all of it the wider path layer, the per-pixel float stamp work and
// packing the touched pixels back to 5-6-5. In the same fleet run that is
// 1.12x plasma, the reference animation BASELINE.md lists first (0.075
// against 0.067 ms). PSRAM goes from about 32 KB to about 160 KB,
// nearly all of it the path layer (11,520 PathPx at 8 B, 92 KB) and the
// 2,880 double ellipse samples it is built from (46 KB); both are built
// once per parameter or theme change, not per frame. The hot slab holds
// about 6 KB of the 9,216 B an animation gets, and the lifecycle check
// reports no fallback to PSRAM.
//
// Device numbers (gm-4bd.12, 2026-10-01, bench board, display-loadtest at
// e35349b7-dirty, pixel clock divider 6 live, interlace pinned on, cap 60,
// tools/framefn_sweep.py with plasma in the same run, both orders): band
// 5.6 and 5.9 ms a frame against plasma's 5.2, so 1.08x to 1.13x, which is
// the host's 1.12x. /api/debug/animtest: 0 differing pixels over 8 frames
// and 3 parameter sets, hot_fail 0 with Orbits resident. What the host did
// not predict is frame(): 9.6 and 12.3 ms a frame against plasma's 0.3,
// and the loop ran 26.0 fps against plasma's 43.6 under the same cap. The
// sample loop above does, per sample, double cos, sin, pow, floor, ceil
// and a divide, about 100 samples a frame, and the S3 has no double FPU,
// so every one is a soft-float libcall. It is the page's arithmetic in the
// page's precision (gm-pciz), so moving it to float or to a table changes
// the goldens and is a decision, not a fix: gm-4bd.13 holds it.
//
// The kernel parity sweep is not an open item, and this paragraph is here
// so nobody re-runs it blind. gm-4bd.11 ran the fleet's band() against
// bandRef() check on the board at affbc12f, which is before this change,
// so its orbits row looks stale. It is not. Everything this rewrite touched
// is in the shared code both paths call: bandRef() is the same drawOverlay
// walk band() uses, so the two cannot have diverged by construction. The
// one hand-written kernel in this file, fillBgPie, was not touched, and the
// QEMU check still reports it bit exact over 40 cases. Re-running the sweep
// for orbits is cheap and is worth doing once on the next board session for
// the record, but it is a confirmation, not a risk being carried.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>
#include <string.h>

namespace {
using namespace bganim;

constexpr int MAX_ORBITS = 6;
constexpr int PATH_SAMPLES = 480;
// A sample makes at most four distinct path pixels and touches at most two
// rows, so these two are hard bounds rather than caps: neither table can
// truncate, whatever the sliders do. The old layer had a real cap of 32 path
// points per ring per 16-row bin, which the ellipse's flat top and bottom
// can exceed, and it dropped the excess silently.
constexpr int MAX_PATH_PX = 4 * PATH_SAMPLES * MAX_ORBITS;  // 11,520
constexpr int MAX_ROW_REFS = 2 * PATH_SAMPLES * MAX_ORBITS; // 5,760
constexpr int NUM_BANDS = 30;
// Cap on orbit-body samples registered per 16-row spatial bin. A cap this
// generous only truncates if most of one frame's stamps land in the same
// bin, which would need every ring's trail to sit near its ellipse's
// flattest point at the same y simultaneously.
constexpr int SAMPLE_BIN_CAP = 48;
constexpr int MAX_SAMPLES = MAX_ORBITS * 18;
constexpr double GOLDEN = 0.6180339887;
// Innermost orbit's period at Speed 50, in seconds; each further orbit is
// 1.618x slower. It was 6.0 until 2026-09-12 (gm-kh2s), which put the half
// change time at 119 ms against the fleet's 1200 ms target and moved the
// innermost body 4.6 px per 66 ms frame, a dotted trail rather than travel.
// The trail dots are spaced in phase (T/90), so the picture at rest is the
// same at any period; only the rate changed.
constexpr double BASE_PERIOD_S = 72.0;
// The page's path alpha before the bilinear weights.
constexpr double PATH_ALPHA = 0.11;
// Float error against the page's double is under 3e-5 of a unit anywhere in
// these expressions, so a result further than this from a half-unit
// boundary rounds the same either way. Three times the error, as in
// AnimFireflies.cpp.
constexpr float HALF = 2e-3f;

struct OrbitDef {
    double a, b, cosPhi, sinPhi, T, phase;
    uint8_t colR, colG, colB;
};

// One of the 480 ellipse samples of one ring, at the resolution the path
// layer was built for.
struct PathSample {
    double xf, yf;
};

// One pixel of the path layer: the 8-bit colour the page has there after
// this ring's samples, and the ring's combined alpha at that pixel for the
// rare case where a trail dot is underneath (see the header).
struct PathPx {
    uint16_t x;
    uint8_t r, g, b, orbit;
    uint16_t keepQ16; // 65536 * the product of (1 - a) over this pixel's samples
};
static_assert(sizeof(PathPx) == 8, "PathPx is read once per path pixel per frame");

// One body or trail dot, as the page draws it. The centre is kept split
// into a whole pixel and a fraction for the float path (the page's
// distance is measured from the pixel corner, and a float centre near 240
// would lose the low bits of it) and as a double for the fallback.
struct Sample {
    double xD, yD;
    double radiusD, alphaD;
    float xFrac, yFrac;
    float invR, alphaF;
    int xInt, yInt;
    int x0, x1; // the row span the stamp can reach, clipped to the panel
};

// Per-sample fields the band scan reads for every candidate: small, hot.
struct SampleMeta {
    int16_t y0, y1;
    uint8_t orbit;
};

OrbitDef orbits[MAX_ORBITS];
int orbitCount = 0;

// The path layer, and its row index. rowStart[y] .. rowStart[y + 1] are the
// pixels of row y, in ring order, which is the page's drawing order.
PathPx *pathPx = nullptr;   // [MAX_PATH_PX], PSRAM
uint16_t *rowStart = nullptr; // [h + 1], hot: one read per row per band call
int pathPxCount = 0;

// Build-time only, all PSRAM: the ellipse samples, their row references
// counting-sorted by row, and the per-row working state.
PathSample *pathSamples = nullptr; // [MAX_ORBITS * PATH_SAMPLES]
uint16_t *rowRefs = nullptr;       // [MAX_ROW_REFS], sample indices by row
uint16_t *rowRefStart = nullptr;   // [h + 1]
uint32_t *rowMark = nullptr;       // [w], plane initialised for this row
uint32_t *orbMark = nullptr;       // [w], touched by the ring being built
float *keepRow = nullptr;          // [w], running product of (1 - a)
uint16_t *orbTouched = nullptr;    // [w], the pixels this ring touched

// Per-frame stamps.
Sample *samples = nullptr;    // [MAX_SAMPLES], PSRAM
SampleMeta *sampleMeta = nullptr; // [MAX_SAMPLES], hot
int sampleCount = 0;
uint8_t *sampleBinCount = nullptr; // [NUM_BANDS], hot
uint8_t *sampleBinIdx = nullptr;   // [NUM_BANDS][SAMPLE_BIN_CAP], hot

// The row being composited, in the page's 8 bits. flags bit 0 means the
// plane holds a real value, bit 1 means a body or trail dot has written it.
uint8_t *planeR = nullptr, *planeG = nullptr, *planeB = nullptr; // [w], hot
uint8_t *flags = nullptr;   // [w], hot
uint16_t *dirty = nullptr;  // [w], hot

int allocW = 0, allocH = 0;
int lastCountP = -1, lastEccP = -1, lastSizeP = -1, lastPathP = -1, lastTiltP = -1;
uint32_t lastThemeGen = 0xFFFFFFFF;
int geomW = 0, geomH = 0;
uint8_t g_bgR = 0, g_bgG = 0, g_bgB = 0;
uint16_t g_bg = 0;

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

// The page's four slider maps, in double, each exactly 1 at 50 so every
// default reproduces the value this animation hard-coded before them.
double sizeMulOf(int v) { return v < 50 ? 0.40 + v * (0.60 / 50) : 1 + (v - 50) * (0.35 / 50); }
double pathMulOf(int v) { return v < 50 ? v / 50.0 : 1 + (v - 50) * (3.0 / 50); }
double glowMulOf(int v) { return v < 50 ? 0.30 + v * (0.70 / 50) : 1 + (v - 50) * (1.6 / 50); }

void release();

// One bilinear contribution of one path sample, applied the page's way.
// The value is computed in float and redone in double whenever it lands
// near a rounding boundary.
BGANIM_INLINE int pathBlend(int base, int col, float aF, double aD) {
    const float v = static_cast<float>(base) * (1.0f - aF) + static_cast<float>(col) * aF;
    const float fr = v - static_cast<float>(static_cast<int>(v));
    if (fabsf(fr - 0.5f) < HALF) {
        return clampRound(static_cast<double>(base) * (1.0 - aD) + static_cast<double>(col) * aD);
    }
    return v <= 0.0f ? 0 : (v >= 255.0f ? 255 : static_cast<int>(v + 0.5f));
}

void buildPathLayer(int w, int h, double pathMul) {
    pathPxCount = 0;
    // The ellipse samples, in the page's order and its expression order.
    for (int i = 0; i < orbitCount; i++) {
        const OrbitDef &o = orbits[i];
        PathSample *out = &pathSamples[i * PATH_SAMPLES];
        const double cx = w / 2.0, cy = h / 2.0;
        for (int s = 0; s < PATH_SAMPLES; s++) {
            const double u = s / 480.0 * M_PI * 2;
            const double cu = cos(u), su = sin(u);
            out[s].xf = cx + o.a * cu * o.cosPhi - o.b * su * o.sinPhi;
            out[s].yf = cy + o.a * cu * o.sinPhi + o.b * su * o.cosPhi;
        }
    }

    // Counting-sort each sample's two row references by row. The pass that
    // places them walks the samples in ring-then-index order, so within a
    // row the references come out in the page's drawing order, and one
    // ring's references are contiguous.
    const int nSamples = orbitCount * PATH_SAMPLES;
    memset(rowRefStart, 0, static_cast<size_t>(h + 1) * sizeof(uint16_t));
    for (int idx = 0; idx < nSamples; idx++) {
        const int y0 = static_cast<int>(floor(pathSamples[idx].yf));
        if (y0 >= 0 && y0 < h) rowRefStart[y0]++;
        if (y0 + 1 >= 0 && y0 + 1 < h) rowRefStart[y0 + 1]++;
    }
    int run = 0;
    for (int y = 0; y < h; y++) {
        const int c = rowRefStart[y];
        rowRefStart[y] = static_cast<uint16_t>(run);
        run += c;
    }
    rowRefStart[h] = static_cast<uint16_t>(run);
    // cursor[] would be a second h-sized table; walk rowStart as the cursor
    // instead and rebuild it from rowRefStart afterwards.
    for (int y = 0; y <= h; y++) rowStart[y] = rowRefStart[y];
    for (int idx = 0; idx < nSamples; idx++) {
        const int y0 = static_cast<int>(floor(pathSamples[idx].yf));
        if (y0 >= 0 && y0 < h) rowRefs[rowStart[y0]++] = static_cast<uint16_t>(idx);
        if (y0 + 1 >= 0 && y0 + 1 < h) rowRefs[rowStart[y0 + 1]++] = static_cast<uint16_t>(idx);
    }

    // Replay the page's accumulation row by row. The planes hold the page's
    // 8-bit picture for the row, carried from one ring to the next.
    static uint32_t gen = 0;
    for (int y = 0; y < h; y++) {
        rowStart[y] = static_cast<uint16_t>(pathPxCount);
        int r = rowRefStart[y];
        const int rEnd = rowRefStart[y + 1];
        gen++;
        const uint32_t rowGen = gen;
        for (int i = 0; i < orbitCount && r < rEnd; i++) {
            if (rowRefs[r] / PATH_SAMPLES != static_cast<unsigned>(i)) continue;
            const OrbitDef &o = orbits[i];
            gen++;
            const uint32_t orbGen = gen;
            int nTouched = 0;
            while (r < rEnd && rowRefs[r] / PATH_SAMPLES == static_cast<unsigned>(i)) {
                const PathSample &ps = pathSamples[rowRefs[r++]];
                const int x0 = static_cast<int>(floor(ps.xf));
                const int py0 = static_cast<int>(floor(ps.yf));
                const double fx = ps.xf - x0, fy = ps.yf - py0;
                const double fyTerm = (y == py0) ? 1 - fy : fy;
                for (int ox = 0; ox <= 1; ox++) {
                    const int xx = x0 + ox;
                    if (xx < 0 || xx >= w) continue;
                    const double aD = PATH_ALPHA * pathMul * (ox ? fx : 1 - fx) * fyTerm;
                    if (rowMark[xx] != rowGen) {
                        rowMark[xx] = rowGen;
                        planeR[xx] = g_bgR;
                        planeG[xx] = g_bgG;
                        planeB[xx] = g_bgB;
                    }
                    if (orbMark[xx] != orbGen) {
                        orbMark[xx] = orbGen;
                        keepRow[xx] = 1.0f;
                        orbTouched[nTouched++] = static_cast<uint16_t>(xx);
                    }
                    const float aF = static_cast<float>(aD);
                    keepRow[xx] *= 1.0f - aF;
                    planeR[xx] = static_cast<uint8_t>(pathBlend(planeR[xx], o.colR, aF, aD));
                    planeG[xx] = static_cast<uint8_t>(pathBlend(planeG[xx], o.colG, aF, aD));
                    planeB[xx] = static_cast<uint8_t>(pathBlend(planeB[xx], o.colB, aF, aD));
                }
            }
            for (int k = 0; k < nTouched && pathPxCount < MAX_PATH_PX; k++) {
                const int xx = orbTouched[k];
                float keep = keepRow[xx] * 65536.0f;
                if (keep < 0) keep = 0;
                if (keep > 65535.0f) keep = 65535.0f;
                PathPx &e = pathPx[pathPxCount++];
                e.x = static_cast<uint16_t>(xx);
                e.r = planeR[xx];
                e.g = planeG[xx];
                e.b = planeB[xx];
                e.orbit = static_cast<uint8_t>(i);
                e.keepQ16 = static_cast<uint16_t>(keep);
            }
        }
    }
    rowStart[h] = static_cast<uint16_t>(pathPxCount);
}

void rebuildGeometry(int countP, int eccP, int sizeP, int pathP, int tiltP, int w, int h) {
    geomW = w;
    geomH = h;
    // Rounded, not truncated. The page rounds (Math.round(3 + p / 100 * 3))
    // and this used to truncate, so at the default slider of 55 the panel
    // drew four rings where the design has five (gm-pciz). The count also
    // sets every ring's radius through the 0.62 / (orbitCount - 1) step
    // below, so one off here moves all of them.
    orbitCount = 3 + (countP * 3 + 50) / 100;
    if (orbitCount < 3) orbitCount = 3;
    if (orbitCount > MAX_ORBITS) orbitCount = MAX_ORBITS;
    const double bRatio = 0.95 - (eccP / 100.0) * 0.4;
    // Orbit size scales the whole ring set about the panel centre, 0.40 at
    // slider 0 to 1.35 at 100. The top is not symmetric with the bottom on
    // purpose: at 1.35 the outer ellipse crosses the rim and its path is
    // clipped away by the bounds checks below, while the inner rings still
    // fill the frame. Much past that the panel empties.
    const double maxR = (w < h ? w : h) * 0.46 * sizeMulOf(sizeP);
    // Angle between one orbit's major axis and the next. 0 stacks every
    // ellipse on the same axis (a nested, aligned orrery), 4.8 rad fans them
    // out differently from the default 2.4.
    const double phiStep = 2.4 * (tiltP / 50.0);
    uint8_t bgC[3];
    themeRGB(3, bgC);
    g_bgR = bgC[0];
    g_bgG = bgC[1];
    g_bgB = bgC[2];
    g_bg = rgb565(bgC[0], bgC[1], bgC[2]);
    for (int i = 0; i < orbitCount; i++) {
        OrbitDef &o = orbits[i];
        o.a = maxR * (0.30 + i * (0.62 / (orbitCount - 1)));
        o.b = o.a * bRatio;
        const double phi = i * phiStep;
        o.cosPhi = cos(phi);
        o.sinPhi = sin(phi);
        o.T = BASE_PERIOD_S * pow(1.0 + GOLDEN, static_cast<double>(i));
        o.phase = i * 1.7;
        // Bodies sample the theme's upper range, spread so neighbours differ.
        uint8_t col[3];
        themeRGB(140 + (i * 115) / (MAX_ORBITS - 1), col);
        o.colR = col[0];
        o.colG = col[1];
        o.colB = col[2];
    }
    // Path glow: how strongly the traced ellipse stands off the background.
    // 0 blends nothing, so the paths disappear and only the bodies remain.
    double pathMul = pathMulOf(pathP);
    if (pathMul < 0) pathMul = 0;
    buildPathLayer(w, h, pathMul);
}

bool init(int w, int h) {
    if (w <= 0 || h <= 0) {
        release();
        return false;
    }
    if (allocW == w && allocH == h && pathPx != nullptr) return true;
    release();
    allocW = w;
    allocH = h;
    // PSRAM: the path layer is read once per path pixel per frame in row
    // order, a short sequential burst per row the cache prefetches, and the
    // build tables are touched only when a slider or the theme moves.
    pathPx = static_cast<PathPx *>(alloc(MAX_PATH_PX * sizeof(PathPx)));
    pathSamples = static_cast<PathSample *>(alloc(MAX_ORBITS * PATH_SAMPLES * sizeof(PathSample)));
    rowRefs = static_cast<uint16_t *>(alloc(MAX_ROW_REFS * sizeof(uint16_t)));
    rowRefStart = static_cast<uint16_t *>(alloc(static_cast<size_t>(h + 1) * sizeof(uint16_t)));
    rowMark = static_cast<uint32_t *>(alloc(static_cast<size_t>(w) * sizeof(uint32_t)));
    orbMark = static_cast<uint32_t *>(alloc(static_cast<size_t>(w) * sizeof(uint32_t)));
    keepRow = static_cast<float *>(alloc(static_cast<size_t>(w) * sizeof(float)));
    orbTouched = static_cast<uint16_t *>(alloc(static_cast<size_t>(w) * sizeof(uint16_t)));
    samples = static_cast<Sample *>(alloc(MAX_SAMPLES * sizeof(Sample)));
    // Hot: read per row or per band call, all of it small.
    rowStart = static_cast<uint16_t *>(allocHot(static_cast<size_t>(h + 1) * sizeof(uint16_t)));
    sampleMeta = static_cast<SampleMeta *>(allocHot(MAX_SAMPLES * sizeof(SampleMeta)));
    sampleBinCount = static_cast<uint8_t *>(allocHot(NUM_BANDS));
    sampleBinIdx = static_cast<uint8_t *>(allocHot(NUM_BANDS * SAMPLE_BIN_CAP));
    planeR = static_cast<uint8_t *>(allocHot(static_cast<size_t>(w)));
    planeG = static_cast<uint8_t *>(allocHot(static_cast<size_t>(w)));
    planeB = static_cast<uint8_t *>(allocHot(static_cast<size_t>(w)));
    flags = static_cast<uint8_t *>(allocHot(static_cast<size_t>(w)));
    dirty = static_cast<uint16_t *>(allocHot(static_cast<size_t>(w) * sizeof(uint16_t)));
    if (!pathPx || !pathSamples || !rowRefs || !rowRefStart || !rowMark || !orbMark || !keepRow || !orbTouched ||
        !samples || !rowStart || !sampleMeta || !sampleBinCount || !sampleBinIdx || !planeR || !planeG || !planeB ||
        !flags || !dirty) {
        release(); // a partial set must not survive a failed init (gm-bzu.15)
        return false;
    }
    memset(rowMark, 0, static_cast<size_t>(w) * sizeof(uint32_t));
    memset(orbMark, 0, static_cast<size_t>(w) * sizeof(uint32_t));
    memset(flags, 0, static_cast<size_t>(w));
    memset(sampleBinCount, 0, NUM_BANDS);
    rebuildGeometry(55, 55, 50, 50, 50, w, h);
    lastCountP = 55;
    lastEccP = 55;
    lastSizeP = 50;
    lastPathP = 50;
    lastTiltP = 50;
    lastThemeGen = themeGen();
    return true;
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[BG_ANIM_PARAMS]) {
    if (p[1] != lastCountP || p[2] != lastEccP || p[4] != lastSizeP || p[5] != lastPathP || p[7] != lastTiltP ||
        themeGen() != lastThemeGen || w != geomW || h != geomH) {
        rebuildGeometry(p[1], p[2], p[4], p[5], p[7], w, h);
        lastCountP = p[1];
        lastEccP = p[2];
        lastSizeP = p[4];
        lastPathP = p[5];
        lastTiltP = p[7];
        lastThemeGen = themeGen();
    }
    const double spd = speedMul(p[0]);
    const double trailAmt = p[3] / 100.0;
    // Body glow scales both the radius and the opacity of every stamp, the
    // body and each trail dot alike, which is what makes the slider read as
    // brightness and not only as size.
    const double glowMul = glowMulOf(p[6]);
    const int K = static_cast<int>(floor(6 + trailAmt * 10 + 0.5));
    const double t = tMs * 0.001;
    const double cx = w / 2.0, cy = h / 2.0;

    sampleCount = 0;
    memset(sampleBinCount, 0, NUM_BANDS);
    for (int i = 0; i < orbitCount; i++) {
        const OrbitDef &o = orbits[i];
        const double T = o.T / spd;
        const double dt = T / 90;
        for (int k = K; k >= 0; k--) {
            if (sampleCount >= MAX_SAMPLES) break;
            const double u = (t - k * dt) / T * M_PI * 2 + o.phase;
            const double cu = cos(u), su = sin(u);
            const double ex = cx + o.a * cu * o.cosPhi - o.b * su * o.sinPhi;
            const double ey = cy + o.a * cu * o.sinPhi + o.b * su * o.cosPhi;
            const double f = 1 - static_cast<double>(k) / (K + 1);
            const double radius = ((k == 0) ? 2.6 : 1.2 * f + 0.4) * glowMul;
            const double alpha = ((k == 0) ? 0.95 : 0.55 * pow(f, 1.6) * (0.4 + trailAmt * 0.8)) * glowMul;
            if (radius <= 0) continue;
            const int idx = sampleCount++;
            Sample &sm = samples[idx];
            sm.xD = ex;
            sm.yD = ey;
            sm.radiusD = radius;
            sm.alphaD = alpha;
            sm.xInt = static_cast<int>(floor(ex));
            sm.yInt = static_cast<int>(floor(ey));
            sm.xFrac = static_cast<float>(ex - sm.xInt);
            sm.yFrac = static_cast<float>(ey - sm.yInt);
            sm.invR = static_cast<float>(1.0 / radius);
            sm.alphaF = static_cast<float>(alpha);
            // The page's own box is ceil(radius * 2.2) wide, but nothing
            // outside |distance| < radius survives its coverage test, so one
            // pixel of slack past ceil(radius) covers the same set: the box
            // from floor(centre) reaches centre - radius on one side and
            // centre + radius on the other for any fraction.
            const int rr = static_cast<int>(ceil(radius)) + 1;
            sm.x0 = sm.xInt - rr < 0 ? 0 : sm.xInt - rr;
            sm.x1 = sm.xInt + rr > w - 1 ? w - 1 : sm.xInt + rr;
            SampleMeta &me = sampleMeta[idx];
            me.orbit = static_cast<uint8_t>(i);
            me.y0 = static_cast<int16_t>(sm.yInt - rr < 0 ? 0 : sm.yInt - rr);
            me.y1 = static_cast<int16_t>(sm.yInt + rr > h - 1 ? h - 1 : sm.yInt + rr);
            // Register into every 16-row bin the stamp can reach. A stamp
            // entirely off the panel still needs a valid bin; band() rechecks
            // the exact row reach before drawing.
            auto clampBand = [](int yy) -> int {
                if (yy < 0) return 0;
                if (yy >= NUM_BANDS * 16) return NUM_BANDS - 1;
                return yy / 16;
            };
            const int binLo = clampBand(me.y0), binHi = clampBand(me.y1);
            for (int b = binLo; b <= binHi; b++) {
                uint8_t &n = sampleBinCount[b];
                if (n < SAMPLE_BIN_CAP) {
                    sampleBinIdx[b * SAMPLE_BIN_CAP + n] = static_cast<uint8_t>(idx);
                    n++;
                }
            }
        }
    }
}

// One stamp pixel in double, in the page's expression order. Used where the
// float path lands near a rounding boundary.
bool stampPixelDouble(const Sample &sm, const OrbitDef &o, int xx, int y) {
    const double ddx = xx - sm.xD, ddy = y - sm.yD;
    const double cov = 1 - sqrt(ddx * ddx + ddy * ddy) / sm.radiusD;
    if (cov <= 0) return false;
    const double a = sm.alphaD * cov * cov;
    planeR[xx] = static_cast<uint8_t>(clampRound(planeR[xx] * (1 - a) + o.colR * a));
    planeG[xx] = static_cast<uint8_t>(clampRound(planeG[xx] * (1 - a) + o.colG * a));
    planeB[xx] = static_cast<uint8_t>(clampRound(planeB[xx] * (1 - a) + o.colB * a));
    return true;
}

// The page's drawGlow over one row, into the planes.
void stampRow(const Sample &sm, const OrbitDef &o, int y, int &nDirty) {
    const float ddy = static_cast<float>(y - sm.yInt) - sm.yFrac;
    const float dy2 = ddy * ddy;
    for (int xx = sm.x0; xx <= sm.x1; xx++) {
        const float ddx = static_cast<float>(xx - sm.xInt) - sm.xFrac;
        const float d2 = ddx * ddx + dy2;
        const float dist = d2 > 0 ? d2 * rsqrtF(d2) : 0.0f;
        const float cov = 1.0f - dist * sm.invR;
        // A pixel within float error of the rim carries an alpha under
        // 1e-8, so whether the page keeps it or drops it, the stored byte
        // is the one already there.
        if (cov <= 0.0f) continue;
        if (!(flags[xx] & 1)) {
            flags[xx] |= 1;
            planeR[xx] = g_bgR;
            planeG[xx] = g_bgG;
            planeB[xx] = g_bgB;
            dirty[nDirty++] = static_cast<uint16_t>(xx);
        }
        const float a = sm.alphaF * cov * cov;
        const float ia = 1.0f - a;
        const float vr = planeR[xx] * ia + o.colR * a;
        const float vg = planeG[xx] * ia + o.colG * a;
        const float vb = planeB[xx] * ia + o.colB * a;
        const float fr = vr - static_cast<float>(static_cast<int>(vr));
        const float fg = vg - static_cast<float>(static_cast<int>(vg));
        const float fb = vb - static_cast<float>(static_cast<int>(vb));
        if (fabsf(fr - 0.5f) < HALF || fabsf(fg - 0.5f) < HALF || fabsf(fb - 0.5f) < HALF) {
            // The flag says a dot was really drawn here, so a later ring's
            // path pixel blends rather than replaces; a pixel the double
            // pass drops at the rim has not been drawn on.
            if (stampPixelDouble(sm, o, xx, y)) flags[xx] |= 2;
            continue;
        }
        flags[xx] |= 2;
        planeR[xx] = static_cast<uint8_t>(vr >= 255.0f ? 255 : static_cast<int>(vr + 0.5f));
        planeG[xx] = static_cast<uint8_t>(vg >= 255.0f ? 255 : static_cast<int>(vg + 0.5f));
        planeB[xx] = static_cast<uint8_t>(vb >= 255.0f ? 255 : static_cast<int>(vb + 0.5f));
    }
}

// Path pixels and orbit bodies over one row of the band, in the page's
// order: ring 0's path, ring 0's stamps, ring 1's path, and so on.
// Identical for band() and bandRef(), so this is the ONE place either path
// draws overlays and pixel-exactness between the PIE-fill kernel and the
// portable reference follows from construction.
void drawOverlayRow(uint16_t *dst, int y, int w) {
    if (y < 0 || y >= geomH || geomW != w) return;
    int nDirty = 0;
    int e = rowStart[y];
    const int eEnd = rowStart[y + 1];
    const int bandIdx = y / 16;
    const uint8_t nS = (bandIdx >= 0 && bandIdx < NUM_BANDS) ? sampleBinCount[bandIdx] : 0;
    const uint8_t *binIdx = &sampleBinIdx[bandIdx * SAMPLE_BIN_CAP];
    for (int i = 0; i < orbitCount; i++) {
        const OrbitDef &o = orbits[i];
        while (e < eEnd && pathPx[e].orbit == i) {
            const PathPx &px = pathPx[e++];
            const int xx = px.x;
            if (xx >= w) continue;
            if (!(flags[xx] & 1)) {
                flags[xx] |= 1;
                dirty[nDirty++] = static_cast<uint16_t>(xx);
                planeR[xx] = px.r;
                planeG[xx] = px.g;
                planeB[xx] = px.b;
                continue;
            }
            if (!(flags[xx] & 2)) {
                planeR[xx] = px.r;
                planeG[xx] = px.g;
                planeB[xx] = px.b;
                continue;
            }
            // A trail dot of an earlier ring is underneath, so the colour
            // built over the background is not the answer; blend with this
            // ring's combined alpha instead (see the header).
            const double keep = px.keepQ16 * (1.0 / 65536.0);
            const double a = 1 - keep;
            planeR[xx] = static_cast<uint8_t>(clampRound(planeR[xx] * keep + o.colR * a));
            planeG[xx] = static_cast<uint8_t>(clampRound(planeG[xx] * keep + o.colG * a));
            planeB[xx] = static_cast<uint8_t>(clampRound(planeB[xx] * keep + o.colB * a));
        }
        for (int si = 0; si < nS; si++) {
            const int idx = binIdx[si];
            const SampleMeta &me = sampleMeta[idx];
            if (me.orbit != i || y < me.y0 || y > me.y1) continue;
            stampRow(samples[idx], o, y, nDirty);
        }
    }
    for (int k = 0; k < nDirty; k++) {
        const int xx = dirty[k];
        dst[xx] = rgb565(planeR[xx], planeG[xx], planeB[xx]);
        flags[xx] = 0;
    }
}

void drawOverlays(uint16_t *dst, int y0, int rows, int w) {
    for (int r = 0; r < rows; r++) {
        drawOverlayRow(dst + static_cast<size_t>(r) * w, y0 + r, w);
    }
}

// Portable spec: flat background fill (scalar), then the shared overlay
// pass. This is what the host bench runs against golden/, and what the
// device's on-chip equivalence test (SleepAnimation::runAnimTest) compares
// band()'s kernel output against pixel for pixel.
void bandRef(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const uint16_t bg = g_bg;
    const int total = rows * w;
    for (int i = 0; i < total; i++) {
        dst[i] = bg;
    }
    drawOverlays(dst, y0, rows, w);
}

#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
// Flat-fill nOct groups of eight RGB565 pixels (128 bits) with bg, on the
// ESP32-S3's PIE vector unit. This is the whole cost of band() that scales
// with pixel count: 480x480 at 25fps is 230,400 background writes a frame,
// every one of them the same constant, so it is the "obvious PIE store loop"
// this file's per-pixel work otherwise has none of (path pixels and orbit
// bodies are sparse scatter, a few thousand writes a frame combined).
//
// dst must be 16-byte aligned and nOct*8 must equal rows*w exactly: both
// hold for every caller here (band buffer rows are 16-byte aligned per
// CLAUDE.md; w is 480 or 240 and rows is 1, 2 or 8 on every real caller, so
// rows*w is always a multiple of 8).
//
// PIE has no scalar-broadcast-into-lanes instruction (ASM_BRIEF's
// ee.movi.32.q sets one 32-bit lane pair at a time, four instructions to
// fill all eight lanes -- no better than this). Building the eight-times
// value in a small 16-byte aligned stack buffer and loading it once with
// ee.vld.128.ip is the idiom ASM_BRIEF recommends instead, and it is what
// every other kernel in this codebase does for a runtime (non-compile-time)
// constant.
//
// PIE is coprocessor CP3, thread context only; band() runs on the SleepAnim
// render task, never an ISR, so this holds (see the longer version of this
// note above SleepAnimation.cpp's scale565Oct). The compiler never touches
// q registers, so no clobber list entry exists for them.
__attribute__((noinline)) static void fillBgPie(uint16_t *dst, int nOct, uint16_t bg) {
    alignas(16) uint16_t bcast[8] = {bg, bg, bg, bg, bg, bg, bg, bg};
    uint16_t *wr = dst;
    const uint16_t *src = bcast;
    int n = nOct;
    asm volatile("ee.vld.128.ip q0, %[src], 0\n" // q0 = bg x8, resident for the whole loop
                 "1:\n"
                 "ee.vst.128.ip q0, %[wr], 16\n"
                 "addi %[n], %[n], -1\n"
                 "bnez %[n], 1b\n"
                 : [wr] "+r"(wr), [src] "+r"(src), [n] "+r"(n)
                 :
                 : "memory");
}
#endif

// Device path: PIE-fill the background, then the shared scalar overlay pass
// (sparse scatter -- path pixels and orbit-body stamps -- stays scalar; see
// ASM_BRIEF's note that a gather/scatter shape with no vector equivalent is
// written by hand in scalar form, not forced into PIE). Everything after the
// fill is byte-for-byte the same code bandRef() runs, so kernel-vs-reference
// pixel-exactness holds by construction rather than by a separately
// maintained duplicate.
//
// Host / non-Xtensa builds: band() IS bandRef(), not merely equivalent to
// it -- there is no second scalar fill to keep in sync.
void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
    fillBgPie(dst, (rows * w) >> 3, g_bg);
    drawOverlays(dst, y0, rows, w);
#else
    bandRef(dst, y0, rows, w, tMs, p);
#endif
}

void release() {
    releaseTable(pathPx, MAX_PATH_PX * sizeof(PathPx));
    releaseTable(pathSamples, MAX_ORBITS * PATH_SAMPLES * sizeof(PathSample));
    releaseTable(rowRefs, MAX_ROW_REFS * sizeof(uint16_t));
    releaseTable(rowRefStart, static_cast<size_t>(allocH + 1) * sizeof(uint16_t));
    releaseTable(rowMark, static_cast<size_t>(allocW) * sizeof(uint32_t));
    releaseTable(orbMark, static_cast<size_t>(allocW) * sizeof(uint32_t));
    releaseTable(keepRow, static_cast<size_t>(allocW) * sizeof(float));
    releaseTable(orbTouched, static_cast<size_t>(allocW) * sizeof(uint16_t));
    releaseTable(samples, MAX_SAMPLES * sizeof(Sample));
    releaseTable(rowStart, static_cast<size_t>(allocH + 1) * sizeof(uint16_t));
    releaseTable(sampleMeta, MAX_SAMPLES * sizeof(SampleMeta));
    releaseTable(sampleBinCount, static_cast<size_t>(NUM_BANDS));
    releaseTable(sampleBinIdx, static_cast<size_t>(NUM_BANDS) * SAMPLE_BIN_CAP);
    releaseTable(planeR, static_cast<size_t>(allocW));
    releaseTable(planeG, static_cast<size_t>(allocW));
    releaseTable(planeB, static_cast<size_t>(allocW));
    releaseTable(flags, static_cast<size_t>(allocW));
    releaseTable(dirty, static_cast<size_t>(allocW) * sizeof(uint16_t));
    // Every sentinel that gates a rebuild, or init() would hand back
    // reallocated tables that nothing refills.
    lastCountP = lastEccP = lastSizeP = lastPathP = lastTiltP = -1;
    lastThemeGen = 0xFFFFFFFF;
    geomW = geomH = 0;
    allocW = allocH = 0;
    orbitCount = 0;
    sampleCount = 0;
    pathPxCount = 0;
}

} // namespace

extern const BgAnimation bg_anim_orbits;
const BgAnimation bg_anim_orbits = {
    "orbits",
    "Orbits",
    {{"speed", "Speed", 50},
     {"orbitCount", "Orbits", 55},
     {"eccentricity", "Eccentricity", 55},
     {"trail", "Trail", 50},
     {"size", "Orbit size", 50},
     {"path", "Path glow", 50},
     {"glow", "Body glow", 50},
     {"tilt", "Tilt spread", 50}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
