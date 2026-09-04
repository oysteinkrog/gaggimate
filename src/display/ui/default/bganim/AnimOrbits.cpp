#ifndef GAGGIMATE_SIM

// "Orbits" — 3-6 faint elliptical paths with glowing bodies at golden-ratio
// periods and analytic (recomputed, not feedback) fading trails. Sparse
// renderer: flat background, precomputed per-band path points, small
// coverage-AA stamps. Design: anim-geometric (Fable), 2026-08-15.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>
#include <string.h>

namespace {
using namespace bganim;

constexpr int MAX_ORBITS = 6;
constexpr int NUM_BANDS = 30;
constexpr int PTS_PER_BAND = 32;
// Cap on orbit-body samples registered per 16-row spatial bin (see
// sampleBinIdx below). MAX_SAMPLES is 108; a cap this generous only
// truncates if most of one frame's samples land in the same bin, which
// would need every orbit's trail to sit near its ellipse's flattest point
// at the same y simultaneously -- theoretically possible, not seen in
// practice. Truncation just drops the excess sample from that band's draw,
// same silent-cap behavior pathBinCount already uses below.
constexpr int SAMPLE_BIN_CAP = 48;
constexpr float GOLDEN = 0.6180339887f;

struct OrbitDef {
    float a, b, phi, T, phase;
    float cosPhi, sinPhi;
    uint8_t colR, colG, colB;
    uint16_t pathColor565;
};

struct PathPt {
    int16_t x, y;
};

OrbitDef orbits[MAX_ORBITS];
int orbitCount = 0;
PathPt *pathBins = nullptr;   // [orbit][band][pt]
uint8_t *pathBinCount = nullptr; // [orbit][band]
int lastCountP = -1, lastEccP = -1;
uint32_t lastThemeGen = 0xFFFFFFFF;
// Resolution the path points were baked at. pathBins holds literal pixel
// coordinates, and unlike the other animations' tables its SIZE is independent
// of w/h — so a resolution change leaves it allocated, correctly sized, and
// wrong. Nothing else in this file would notice: init() no-ops on a non-null
// pathBins and frame() only rebuilds on a param or theme change.
int geomW = 0, geomH = 0;
int g_w = 480;
uint16_t g_bg = 0;

// Per-frame body + trail samples, computed in frame(), drawn per band.
// r2/invR/rr used to cost one sqrtf-free divide and one ceilf PER SAMPLE
// PER BAND CALL that touched it -- a stamp up to 7px tall spans 3-4 of the
// device's 2-row band() calls, so the same sample paid those libcalls
// several times a frame. Both depend only on radius, which is fixed at
// sample-creation time, so they are computed once here in frame() instead
// (still real libm/divide cost, but O(samples) not O(samples x band calls
// touching them), and frame()-time libm is explicitly fine per OPTIMIZE.md).
struct Sample {
    float x, y, radius, alpha;
    float r2;  // radius*radius, was recomputed per (sample, band-call)
    float invR; // 1/radius, ditto -- one __divsf3 call each, now paid once
    int rr;    // ceilf(radius), ditto -- one libcall each, now paid once
    uint8_t orbit;
};
constexpr int MAX_SAMPLES = MAX_ORBITS * 18;
Sample *samples = nullptr;
int sampleCount = 0;

// Samples binned into the same 16-row grid rebuildGeometry uses for path
// points (bandIdx = y / 16), so band() looks up only the handful of samples
// whose y-reach can touch its call instead of scanning all ~100 per frame,
// 240 times a frame. A sample can straddle a bin edge (bin height 16,
// max reach 2.6px), so it is registered into every bin its [y-radius,
// y+radius] span touches -- almost always one bin, occasionally two.
uint8_t *sampleBinCount = nullptr;           // [NUM_BANDS]
uint8_t *sampleBinIdx = nullptr;             // [NUM_BANDS][SAMPLE_BIN_CAP], indices into samples[]

void rebuildGeometry(int countP, int eccP, int w, int h) {
    geomW = w;
    geomH = h;
    orbitCount = 3 + (countP * 3) / 100;
    const float bRatio = 0.95f - (eccP / 100.0f) * 0.4f;
    const float maxR = (w < h ? w : h) * 0.46f;
    const float cx = w * 0.5f, cy = h * 0.5f;
    memset(pathBinCount, 0, MAX_ORBITS * NUM_BANDS);
    uint8_t bgC[3];
    themeRGB(3, bgC);
    g_bg = rgb565(bgC[0], bgC[1], bgC[2]);
    const uint16_t bg = g_bg;
    for (int i = 0; i < orbitCount; i++) {
        OrbitDef &o = orbits[i];
        o.a = maxR * (0.30f + i * (0.62f / (orbitCount - 1)));
        o.b = o.a * bRatio;
        o.phi = i * 2.4f;
        o.cosPhi = cosf(o.phi);
        o.sinPhi = sinf(o.phi);
        o.T = 6.0f * powf(1.0f + GOLDEN, static_cast<float>(i));
        o.phase = i * 1.7f;
        // Bodies sample the theme's upper range, spread so neighbors differ.
        uint8_t col[3];
        themeRGB(140 + (i * 115) / (MAX_ORBITS - 1), col);
        o.colR = col[0];
        o.colG = col[1];
        o.colB = col[2];
        o.pathColor565 = blendQ8(bg, rgb565(o.colR, o.colG, o.colB), static_cast<int>(0.11f * 256));
        for (int s = 0; s < 480; s++) {
            const float u = s / 480.0f * 6.2831853f;
            const float cu = cosf(u), su = sinf(u);
            const float x = cx + o.a * cu * o.cosPhi - o.b * su * o.sinPhi;
            const float y = cy + o.a * cu * o.sinPhi + o.b * su * o.cosPhi;
            const int bandIdx = static_cast<int>(y) / 16;
            if (bandIdx < 0 || bandIdx >= NUM_BANDS || x < 0 || x >= w) {
                continue;
            }
            uint8_t &n = pathBinCount[i * NUM_BANDS + bandIdx];
            if (n < PTS_PER_BAND) {
                pathBins[(i * NUM_BANDS + bandIdx) * PTS_PER_BAND + n] = {static_cast<int16_t>(x), static_cast<int16_t>(y)};
                n++;
            }
        }
    }
}

bool init(int w, int h) {
    g_w = w;
    // samples[] is read per candidate pixel inside every touched stamp's
    // dx/dy loop (sm.x/y/r2/invR/alpha/orbit, up to ~49 reads per band call
    // a sample overlaps) via an indirect index (sampleBinIdx), so the access
    // pattern is a scattered gather across the whole table rather than a
    // sequential sweep -- exactly the shape the hot-slab comment in
    // BgAnimCommon.h calls out as not benefiting from PSRAM's prefetch.
    // 3,456 B (108 x 32 B), well inside the 9,216 B per-animation budget.
    if (samples == nullptr) {
        samples = static_cast<Sample *>(allocHot(MAX_SAMPLES * sizeof(Sample)));
        if (samples == nullptr) {
            return false;
        }
    }
    // pathBins is 23,040 B, over twice the whole per-animation slab, so it
    // cannot go hot regardless of access pattern -- but its access pattern
    // does not want to: each band() call reads at most PTS_PER_BAND (32)
    // contiguous PathPt entries from one (orbit, bin) run, a small
    // sequential burst the PSRAM cache prefetches, not a scattered gather.
    // pathBinCount is the loop bound for that read (n = pathBinCount[...]),
    // touched once per (orbit, band-call) -- 180 B, trivial to keep hot.
    if (pathBins == nullptr) {
        pathBins = static_cast<PathPt *>(alloc(MAX_ORBITS * NUM_BANDS * PTS_PER_BAND * sizeof(PathPt)));
        pathBinCount = static_cast<uint8_t *>(allocHot(MAX_ORBITS * NUM_BANDS));
        if (pathBins == nullptr || pathBinCount == nullptr) {
            return false;
        }
        rebuildGeometry(55, 55, w, h);
        lastCountP = 55;
        lastEccP = 55;
    }
    // sampleBinCount (30 B) and sampleBinIdx (1,440 B) are read once and
    // nS times respectively per band call (240 calls/frame) to find which
    // samples[] entries apply to this call -- small, frequent, and on the
    // path to every sample read above, so hot for the same reason.
    if (sampleBinCount == nullptr) {
        sampleBinCount = static_cast<uint8_t *>(allocHot(NUM_BANDS));
        sampleBinIdx = static_cast<uint8_t *>(allocHot(NUM_BANDS * SAMPLE_BIN_CAP));
        if (sampleBinCount == nullptr || sampleBinIdx == nullptr) {
            return false;
        }
        memset(sampleBinCount, 0, NUM_BANDS);
    }
    return true;
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[4]) {
    if (p[1] != lastCountP || p[2] != lastEccP || themeGen() != lastThemeGen || w != geomW || h != geomH) {
        rebuildGeometry(p[1], p[2], w, h);
        lastCountP = p[1];
        lastEccP = p[2];
        lastThemeGen = themeGen();
    }
    const float spd = speedMul(p[0]);
    const float trailAmt = p[3] / 100.0f;
    const int K = 6 + static_cast<int>(trailAmt * 10.0f);
    const float t = tMs * 0.001f;
    const float cx = w * 0.5f, cy = h * 0.5f;

    sampleCount = 0;
    memset(sampleBinCount, 0, NUM_BANDS);
    for (int i = 0; i < orbitCount; i++) {
        const OrbitDef &o = orbits[i];
        const float T = o.T / spd;
        const float dt = T / 90.0f;
        for (int k = K; k >= 0; k--) {
            const float u = (t - k * dt) / T * 6.2831853f + o.phase;
            const float cu = fastCosRad(u), su = fastSinRad(u);
            const float ex = cx + o.a * cu * o.cosPhi - o.b * su * o.sinPhi;
            const float ey = cy + o.a * cu * o.sinPhi + o.b * su * o.cosPhi;
            const float f = 1.0f - static_cast<float>(k) / (K + 1);
            const float radius = (k == 0) ? 2.6f : 1.2f * f + 0.4f;
            float alpha = (k == 0) ? 0.95f : 0.55f * f * f * (0.4f + 0.8f * trailAmt); // f^2 ~ f^1.6, no powf
            if (sampleCount < MAX_SAMPLES) {
                // r2/invR/rr computed once here (frame() runs once per frame,
                // not once per band() call) -- see the Sample comment above.
                const float r2 = radius * radius;
                const float invR = 1.0f / radius;
                const int rr = static_cast<int>(ceilf(radius));
                const int idx = sampleCount++;
                samples[idx] = {ex, ey, radius, alpha, r2, invR, rr, static_cast<uint8_t>(i)};
                // Register into every 16-row bin this sample's stamp can
                // reach, clamped into range -- an off-screen sample (rare:
                // trail arcs occasionally cross y=0 or y=h) still needs a
                // valid bin, and clamping to the nearest edge bin is safe
                // because band() re-checks the exact y-reach before drawing.
                auto clampBand = [](float yy) -> int {
                    if (yy < 0.0f) {
                        return 0;
                    }
                    if (yy >= static_cast<float>(NUM_BANDS * 16)) {
                        return NUM_BANDS - 1;
                    }
                    return static_cast<int>(yy) / 16;
                };
                const int binLo = clampBand(ey - radius);
                const int binHi = clampBand(ey + radius);
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
}

// Path-point and orbit-body splats: identical for band() and bandRef(), so
// this is the ONE place either path draws overlays -- pixel-exactness
// between the PIE-fill kernel and the portable reference follows from
// construction rather than needing to be checked pixel by pixel, since the
// only thing that differs between the two callers is how the background got
// filled beforehand.
static void drawOverlays(uint16_t *dst, int y0, int rows, int w) {
    // Path points are pre-binned into fixed 16-row spatial bins by
    // rebuildGeometry, and orbit-body samples are now binned the same way by
    // frame() (see sampleBinIdx above), so exactly ONE bin is consulted per
    // call for both. So band() requires [y0, y0+rows) to lie inside a single
    // bin: rows must divide 16 with y0 a multiple of rows, or rows == 1 at
    // any y. Both real callers satisfy that -- SleepAnimation renders 8-row
    // bands, and its interlaced half-res path renders rows == 1 -- but
    // nothing enforces it, and a caller that straddles a boundary gets no
    // error, just silently missing path pixels for every bin but the first
    // (a 40-row band drops two thirds of them). If a taller band is ever
    // wanted, loop this block over the bins the range covers rather than
    // widening the bins.
    const int bandIdx = y0 / 16;
    // Guards the pathBinCount/pathBins/sampleBinCount/sampleBinIdx indexing
    // below, which is otherwise unbounded in y0. Only these lookups are
    // skipped when out of range; nothing else in the animation depends on it.
    for (int i = 0; bandIdx >= 0 && bandIdx < NUM_BANDS && i < orbitCount; i++) {
        const uint8_t n = pathBinCount[i * NUM_BANDS + bandIdx];
        const PathPt *pts = &pathBins[(i * NUM_BANDS + bandIdx) * PTS_PER_BAND];
        for (int j = 0; j < n; j++) {
            const int ly = pts[j].y - y0;
            // x is bounds-checked as well as y. rebuildGeometry only clips
            // against the w it ran at, so an unchecked store here turns any
            // future staleness into a heap write past the end of the band
            // buffer rather than a visual artefact. One compare per path
            // point, a few hundred per band.
            if (ly >= 0 && ly < rows && pts[j].x >= 0 && pts[j].x < w) {
                dst[static_cast<size_t>(ly) * w + pts[j].x] = orbits[i].pathColor565;
            }
        }
    }

    if (bandIdx < 0 || bandIdx >= NUM_BANDS) {
        return;
    }
    // Only the samples frame() binned into this 16-row slice are candidates
    // -- typically a handful, against scanning all ~100 per-frame samples on
    // every one of the device's 240 band() calls. The y-reach recheck stays:
    // a sample can be binned here because it overlaps SOME sub-range of the
    // bin's 16 rows while this particular call (rows can be 1, 2 or 8) covers
    // a different sub-range that it does not actually reach.
    const uint8_t nS = sampleBinCount[bandIdx];
    const uint8_t *binIdx = &sampleBinIdx[bandIdx * SAMPLE_BIN_CAP];
    for (int si = 0; si < nS; si++) {
        const Sample &sm = samples[binIdx[si]];
        const float reach = sm.radius; // coverage is zero beyond radius
        if (sm.y + reach < y0 || sm.y - reach >= y0 + rows) {
            continue;
        }
        const OrbitDef &o = orbits[sm.orbit];
        const int rr = sm.rr; // cached in frame(): was a ceilf() call per (sample, band-call)
        const int x0i = static_cast<int>(sm.x) - rr, y0i = static_cast<int>(sm.y) - rr;
        const float r2 = sm.r2;     // cached in frame(): was recomputed per (sample, band-call)
        const float invR = sm.invR; // cached in frame(): was a __divsf3 call per (sample, band-call)
        for (int dy = 0; dy <= rr * 2 + 1; dy++) {
            const int yy = y0i + dy - y0;
            if (yy < 0 || yy >= rows) {
                continue;
            }
            for (int dx = 0; dx <= rr * 2 + 1; dx++) {
                const int xx = x0i + dx;
                if (xx < 0 || xx >= w) {
                    continue;
                }
                const float ddx = xx + 0.5f - sm.x, ddy = (y0i + dy) + 0.5f - sm.y;
                const float d2 = ddx * ddx + ddy * ddy;
                if (d2 >= r2) {
                    continue;
                }
                // Alpha-max/beta-min distance approximation (coeffs 0.9604/
                // 0.3978, max error ~4%) instead of sqrtf(d2): the stamp is a
                // 2-3px soft glow blob, so a few-percent wobble on the AA
                // fringe is imperceptible and this is the only per-pixel cost
                // left in the whole animation.
                const float adx = ddx < 0 ? -ddx : ddx, ady = ddy < 0 ? -ddy : ddy;
                const float dist = adx > ady ? (0.9604f * adx + 0.3978f * ady) : (0.9604f * ady + 0.3978f * adx);
                const float cov = 1.0f - dist * invR;
                if (cov <= 0.0f) {
                    continue;
                }
                const int aQ8 = static_cast<int>(sm.alpha * cov * cov * 256.0f);
                if (aQ8 <= 0) {
                    continue;
                }
                uint16_t &px = dst[static_cast<size_t>(yy) * w + xx];
                px = blendQ8(px, rgb565(o.colR, o.colG, o.colB), aQ8 > 256 ? 256 : aQ8);
            }
        }
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
// this file's per-pixel work otherwise has none of (path points and orbit
// bodies are sparse scatter, a few hundred writes a frame combined).
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
// (sparse scatter -- path points and orbit-body stamps -- stays scalar; see
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
    releaseTable(samples, MAX_SAMPLES * sizeof(Sample));
    releaseTable(pathBins, MAX_ORBITS * NUM_BANDS * PTS_PER_BAND * sizeof(PathPt));
    releaseTable(pathBinCount, static_cast<size_t>(MAX_ORBITS) * NUM_BANDS);
    releaseTable(sampleBinCount, static_cast<size_t>(NUM_BANDS));
    releaseTable(sampleBinIdx, static_cast<size_t>(NUM_BANDS) * SAMPLE_BIN_CAP);
    // Every sentinel that gates a rebuild, or init() would hand back
    // reallocated tables that nothing refills.
    lastCountP = lastEccP = -1;
    lastThemeGen = 0xFFFFFFFF;
    geomW = geomH = 0;
    orbitCount = 0;
    sampleCount = 0;
}

} // namespace

extern const BgAnimation bg_anim_orbits;
const BgAnimation bg_anim_orbits = {
    "orbits",
    "Orbits",
    {{"speed", "Speed", 50}, {"orbitCount", "Orbits", 55}, {"eccentricity", "Eccentricity", 55}, {"trail", "Trail", 50}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
