#ifndef GAGGIMATE_SIM

// "Quiet Horizon" - one gently curved horizon dividing two broad theme tones,
// with a soft band of light along its edge, rising and falling slowly.
// Brainstormed 2026-09-09 with GPT (Codex CLI, gpt-6-astra) as candidate 4 of
// gm-4bd.
//
// Same separable kernel as Plasma and Brushed Metal: index =
// (colTermPh[x] + rowTerm[y]) >> 4, one palette gather. The two terms are
// pure geometry. rowTerm is a linear ramp in y plus the height the horizon
// currently sits at, so it is what moves; colTermPh is a shallow quadratic in
// (x - cx), which is what bends the line into an arc, and it only changes when
// the curvature parameter does.
//
// The horizon itself is entirely a palette shape, not a per-pixel test. The
// palette is built once per parameter or theme change with the transition
// pinned at index 128: a dark tone below, a lighter tone above, a smooth
// crossing whose width is the Softness slider, and a rim highlight on top of
// the crossing. Moving the horizon is then just moving rowTerm's offset, which
// slides the whole index field past that fixed palette feature. Nothing per
// pixel knows the horizon exists.
//
// Index safety. band() has no per-pixel bound check, so frame() keeps both
// terms non-negative and clamps colTermPh into [0, 4095 - rowMax] after
// measuring rowTerm's largest value. That holds at every parameter
// combination, which is what the fuzz harness checks with the sanitizers on.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <string.h>

namespace {
using namespace bganim;

constexpr int INDEX_MAX = 4095; // (colTermPh + rowTerm) must stay in 0..INDEX_MAX
// Sum-unit budget, where 16 sum units are one palette index. The vertical
// ramp spans ROW_SPAN from the bottom row to the top, the horizon's height
// offset lives in [HEIGHT_LO, HEIGHT_HI], and the arc bends by at most
// CURVE_SPAN. The largest sum is HEIGHT_HI + ROW_SPAN + CURVE_SPAN plus the
// dither, which is 3,956 of the 4,095 the palette index allows.
constexpr int ROW_SPAN = 1400;
constexpr int HEIGHT_LO = 600;
constexpr int HEIGHT_HI = 1900;
constexpr int HEIGHT_TRAVEL = 210; // the largest drift either side of the mean
constexpr int CURVE_SPAN = 400;

int16_t *colCurve = nullptr;  // frame() only -> PSRAM
int16_t *colTermPh = nullptr; // read every pixel -> slab, 8 y-phase copies
int16_t *rowTerm = nullptr;   // read once per row -> slab
uint16_t *themeRamp = nullptr; // frame() only -> PSRAM
uint16_t *palette = nullptr;  // read every pixel -> slab

int16_t dithOff[64] = {0};

uint8_t lastP[4] = {255, 255, 255, 255};
uint32_t lastThemeGen = 0xFFFFFFFF;
int lastCurveP = -1;
int allocW = 0, allocH = 0;

// Table placement, against the 9,216 B per-animation slab:
//   colTermPh  7,680 B  read every pixel                   HOT
//   rowTerm      960 B  read once per row                  HOT
//   palette      512 B  read every pixel, data-dependent   HOT
//   -------------------------------------------------------------
//              9,152 B of 9,216 B
//   colCurve     960 B  read only in frame()               PSRAM
//   themeRamp    512 B  read only when the palette is
//                       rebuilt                            PSRAM
void release();

bool init(int w, int h) {
    if (sinLut() == nullptr) {
        return false;
    }
    if (colCurve == nullptr) {
        colCurve = static_cast<int16_t *>(alloc(w * sizeof(int16_t)));
        if (colCurve == nullptr) {
            release(); // a partial set must not survive a failed init (gm-bzu.15)
            return false;
        }
        allocW = w;
    }
    if (colTermPh == nullptr) {
        colTermPh = static_cast<int16_t *>(allocHot(8 * allocW * sizeof(int16_t)));
        if (colTermPh == nullptr) {
            release();
            return false;
        }
    }
    if (rowTerm == nullptr) {
        rowTerm = static_cast<int16_t *>(allocHot(h * sizeof(int16_t)));
        if (rowTerm == nullptr) {
            release();
            return false;
        }
        allocH = h;
    }
    if (themeRamp == nullptr) {
        themeRamp = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
        if (themeRamp == nullptr) {
            release();
            return false;
        }
    }
    if (palette == nullptr) {
        palette = static_cast<uint16_t *>(allocHot(256 * sizeof(uint16_t)));
        if (palette == nullptr) {
            release();
            return false;
        }
    }
    return true;
}

// The palette carries the whole look: ground below, sky above, a crossing of
// half-width `soft` centred on index 128, and a rim highlight inside it.
void buildPalette(int softP) {
    buildThemeRamp(themeRamp, 256);
    // Half-width of the crossing in palette indices. The vertical ramp puts
    // about 0.18 of an index on each row, so 2..24 indices is a transition
    // 11 to 130 pixels tall: wide enough to read as soft, narrow enough that
    // the horizon does not dissolve into a plain gradient.
    const int soft = 2 + softP * 22 / 100;
    const int rim = 2 + soft / 4;
    for (int i = 0; i < 256; i++) {
        const int d = i - 128;
        // Smooth crossing from the ground tone to the sky tone. The cubic is
        // the usual smoothstep, evaluated in Q8 so there is no float here.
        int t;
        if (d <= -soft) {
            t = 0;
        } else if (d >= soft) {
            t = 256;
        } else {
            const int u = ((d + soft) << 8) / (2 * soft); // 0..256
            t = (u * u * (768 - 2 * u)) >> 16;            // 3u^2 - 2u^3, Q8
            t = t < 0 ? 0 : (t > 256 ? 256 : t);
        }
        // Ground 24..64 of the theme ramp, sky 96..208, plus a slow gradient
        // inside each so neither half is a flat wash.
        const int ground = 24 + ((i * 40) >> 8);
        const int sky = 96 + ((i * 112) >> 8);
        int v = ground + (((sky - ground) * t) >> 8);
        // Rim: a triangular bump right at the crossing.
        const int ad = d < 0 ? -d : d;
        if (ad < rim) {
            v += ((255 - v) * (rim - ad)) / (2 * rim);
        }
        palette[i] = themeRamp[v < 0 ? 0 : (v > 255 ? 255 : v)];
    }
    // ditherAmp() returns the MEAN step spacing of the palette, and this
    // palette is deliberately not uniform: the two broad tones step slowly and
    // want a large amplitude, the crossing between them steps every index or
    // two and wants almost none. At full amplitude the crossing showed a
    // visible crosshatch in the host render while the flat tones were already
    // clean, so the mean is halved here. The flat tones still dither, which is
    // where the banding actually is.
    const float amp = ditherAmp(palette, 256) * 0.5f;
    for (int k = 0; k < 64; k++) {
        const float d = (static_cast<float>(BAYER8[k]) - 31.5f) * (amp * 16.0f / 31.5f);
        dithOff[k] = static_cast<int16_t>(lroundf(d));
    }
}

// The arc. p[2] is signed about its midpoint, so 0 bends the horizon one way,
// 100 the other and 50 is a straight line. The result is shifted so its
// smallest value is 0, which is what keeps the sum non-negative later.
void buildCurve(int curveP, int w) {
    const int cx = w / 2;
    const int span = cx > 0 ? cx : 1;
    const int k = (curveP - 50) * CURVE_SPAN / 50; // -CURVE_SPAN..CURVE_SPAN
    int lo = 32767;
    for (int x = 0; x < w; x++) {
        const int dx = x - cx;
        // dx*dx/span^2 in Q8, so the edge of the panel is 256 and the centre 0.
        const int q = (dx * dx * 256) / (span * span);
        const int v = (k * (q > 256 ? 256 : q)) >> 8;
        colCurve[x] = static_cast<int16_t>(v);
        if (v < lo) {
            lo = v;
        }
    }
    for (int x = 0; x < w; x++) {
        colCurve[x] = static_cast<int16_t>(colCurve[x] - lo);
    }
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[4]) {
    if (memcmp(p, lastP, 4) != 0 || themeGen() != lastThemeGen) {
        buildPalette(p[3]);
        lastThemeGen = themeGen();
    }
    if (p[2] != lastCurveP) {
        buildCurve(p[2], w);
        lastCurveP = p[2];
    }
    memcpy(lastP, p, 4);

    // Time. Unsigned throughout, so the millis() wrap is a phase wrap.
    const uint32_t sp = 4 + static_cast<uint32_t>(p[0]) * 44 / 100; // 0.25x..3x
    const uint32_t ph = (tMs * sp) >> 12;                           // very slow
    const int16_t *sl = sinLut();
    // Height: the mean comes from the slider, the travel is a slow sine plus a
    // second one at an unrelated rate so the motion never looks like a loop.
    // The mean is inset by the travel, so the sum of the two always lands
    // inside [HEIGHT_LO, HEIGHT_HI] and the drift is never clipped flat at the
    // ends of the slider.
    const int lo = HEIGHT_LO + HEIGHT_TRAVEL;
    const int hiMean = HEIGHT_HI - HEIGHT_TRAVEL;
    const int mean = lo + static_cast<int>(p[1]) * (hiMean - lo) / 100;
    const int drift = ((sl[ph & (SIN_N - 1)] * 150) >> 9) + ((sl[((ph * 3) >> 2) & (SIN_N - 1)] * 60) >> 9);
    int offset = mean + drift;
    if (offset < HEIGHT_LO) {
        offset = HEIGHT_LO;
    }
    if (offset > HEIGHT_HI) {
        offset = HEIGHT_HI;
    }

    const int denom = h > 1 ? h - 1 : 1;
    int rowMax = 0;
    for (int y = 0; y < h; y++) {
        const int v = offset + (h - 1 - y) * ROW_SPAN / denom;
        rowTerm[y] = static_cast<int16_t>(v);
        if (v > rowMax) {
            rowMax = v;
        }
    }

    const int hi = INDEX_MAX - rowMax;
    for (int ph8 = 0; ph8 < 8; ph8++) {
        int16_t *dstPh = colTermPh + static_cast<size_t>(ph8) * w;
        const int16_t *off = &dithOff[ph8 * 8];
        for (int x = 0; x < w; x++) {
            int v = colCurve[x] + off[x & 7];
            v = v < 0 ? 0 : (v > hi ? hi : v);
            dstPh[x] = static_cast<int16_t>(v);
        }
    }
}

// Portable on every target, so the descriptor's bandRef slot is nullptr, which
// is what BgAnim.h asks for when there is no second path to compare against.
void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int y = y0; y < y0 + rows; y++) {
        const int rt = rowTerm[y];
        const int16_t *ct = colTermPh + static_cast<size_t>(y & 7) * w;
        int x = 0;
        for (; x + 1 < w; x += 2) {
            const uint16_t c0 = palette[(ct[x] + rt) >> 4];
            const uint16_t c1 = palette[(ct[x + 1] + rt) >> 4];
            *reinterpret_cast<uint32_t *>(dst) = static_cast<uint32_t>(c0) | (static_cast<uint32_t>(c1) << 16);
            dst += 2;
        }
        for (; x < w; x++) {
            *dst++ = palette[(ct[x] + rt) >> 4];
        }
    }
}

void release() {
    releaseTable(colCurve, static_cast<size_t>(allocW) * sizeof(int16_t));
    releaseTable(colTermPh, static_cast<size_t>(8 * allocW) * sizeof(int16_t));
    releaseTable(rowTerm, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(themeRamp, 256 * sizeof(uint16_t));
    releaseTable(palette, 256 * sizeof(uint16_t));
    allocW = allocH = 0;
    lastThemeGen = 0xFFFFFFFF;
    lastCurveP = -1;
    lastP[0] = lastP[1] = lastP[2] = lastP[3] = 255;
}

} // namespace

extern const BgAnimation bg_anim_horizon;
const BgAnimation bg_anim_horizon = {
    "horizon",
    "Quiet Horizon",
    {{"speed", "Speed", 10}, {"height", "Height", 45}, {"curvature", "Curvature", 35}, {"softness", "Softness", 60}},
    init,
    frame,
    band,
    release,
    nullptr,
};

#endif // GAGGIMATE_SIM
