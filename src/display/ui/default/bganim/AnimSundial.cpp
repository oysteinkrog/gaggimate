#ifndef GAGGIMATE_SIM

// "Silent Sundial" - one broad wedge of shade turning slowly around the centre
// of the panel, over a background that is a soft vertical gradient. The wedge
// carries its own gentle surface shading, so it reads as a solid dial under
// changing light rather than a flat cut-out. Brainstormed 2026-09-09 with GPT
// (Codex CLI, gpt-6-astra) as candidate 7 of gm-4bd.
//
// A run based renderer rather than a full field one. The wedge is a convex
// sector, an angle under 180 degrees intersected with the panel's inscribed
// disc, and a convex set meets any horizontal line in exactly one interval, so
// frame() can store one interval per row and band() never has to test a pixel
// for membership. Inside the run the expression is the separable one,
//
//   palette[(colQ4Ph[(y & 7) * w + x] + rowShadeQ4[y]) >> 4]
//
// two gathers, one add and one shift, with the ordered dither already folded
// into the eight y-phase copies of the column table. Outside the run a row is
// an eight pixel repeat of its own dithered background colour, which is a
// store from a small local array and no gather at all. The 180 degree cap on
// the wedge is what makes "one interval" true, and the width slider's range
// enforces it rather than a runtime check.
//
// The interval endpoints are kept in sixteenths of a pixel so the two end
// pixels of each run can be blended by coverage. That is two blends per row at
// most, paid once per row rather than per pixel, and it is what keeps the
// wedge's two straight edges from stepping as it turns.
//
// The disc half-width per row is pure geometry, so it costs one square root
// per row once per size and then never again. The two straight edges are lines
// through the centre, so per row they are one multiply each against a slope
// frame() computes once.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>
#include <string.h>

namespace {
using namespace bganim;

// Palette index budget for a wedge pixel: rowShadeQ4 carries the base level
// plus the vertical half of the surface shading, colQ4Ph the horizontal half
// plus the dither. The ranges below are chosen so the sum lands inside
// 0..255 for every slider position, which is why band() needs no clamp.
constexpr int WEDGE_BASE_LO = 40;
constexpr int WEDGE_BASE_HI = 130;
constexpr int SHADE_SPAN = 50; // per axis, palette indices
constexpr int BG_LO = 10;      // background gradient, in theme ramp indices
constexpr int BG_SPAN = 30;

int16_t *discHalfQ4 = nullptr; // read once per row in frame() -> PSRAM
uint16_t *spanLoQ4 = nullptr;  // read twice per row in band() -> PSRAM
uint16_t *spanHiQ4 = nullptr;  // read twice per row in band() -> PSRAM
int16_t *bgQ4 = nullptr;       // read once per row in band() -> PSRAM
uint16_t *colQ4Ph = nullptr;   // read every wedge pixel -> slab
int16_t *rowShadeQ4 = nullptr; // the add on every wedge pixel -> slab
uint16_t *palette = nullptr;   // read every wedge pixel -> slab

int16_t dithOff[64] = {0};

uint8_t lastP[4] = {255, 255, 255, 255};
uint32_t lastThemeGen = 0xFFFFFFFF;
bool geomValid = false;
int allocW = 0, allocH = 0;

// Table placement, against the 9,216 B per-animation slab:
//   colQ4Ph    7,680 B  read every wedge pixel             HOT
//   rowShadeQ4   960 B  the add on every wedge pixel       HOT
//   palette      512 B  read every wedge pixel             HOT
//   -------------------------------------------------------------
//              9,152 B of 9,216 B
//   discHalfQ4   960 B  once per row, in frame() only      PSRAM
//   spanLoQ4     960 B  twice per row                      PSRAM
//   spanHiQ4     960 B  twice per row                      PSRAM
//   bgQ4         960 B  once per row                       PSRAM
// The four PSRAM tables are walked in row order, so a band's reads land in one
// cache line each and the fetch is amortised over a whole row of pixels.
void release();

bool init(int w, int h) {
    if (sinLut() == nullptr) {
        return false;
    }
    // Every early return below goes through release(), so a half built set is
    // never left behind for the retry to trip over (gm-bzu.15).
    if (discHalfQ4 == nullptr) {
        discHalfQ4 = static_cast<int16_t *>(alloc(h * sizeof(int16_t)));
        if (discHalfQ4 == nullptr) {
            release();
            return false;
        }
        allocH = h;
    }
    if (spanLoQ4 == nullptr) {
        spanLoQ4 = static_cast<uint16_t *>(alloc(allocH * sizeof(uint16_t)));
        if (spanLoQ4 == nullptr) {
            release();
            return false;
        }
    }
    if (spanHiQ4 == nullptr) {
        spanHiQ4 = static_cast<uint16_t *>(alloc(allocH * sizeof(uint16_t)));
        if (spanHiQ4 == nullptr) {
            release();
            return false;
        }
    }
    if (bgQ4 == nullptr) {
        bgQ4 = static_cast<int16_t *>(alloc(allocH * sizeof(int16_t)));
        if (bgQ4 == nullptr) {
            release();
            return false;
        }
    }
    if (colQ4Ph == nullptr) {
        colQ4Ph = static_cast<uint16_t *>(allocHot(8 * w * sizeof(uint16_t)));
        if (colQ4Ph == nullptr) {
            release();
            return false;
        }
        allocW = w;
    }
    if (rowShadeQ4 == nullptr) {
        rowShadeQ4 = static_cast<int16_t *>(allocHot(allocH * sizeof(int16_t)));
        if (rowShadeQ4 == nullptr) {
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

void frame(uint32_t tMs, int w, int h, const uint8_t p[BG_ANIM_PARAMS]) {
    const bool paramsChanged = memcmp(p, lastP, 4) != 0;
    const bool themeChanged = themeGen() != lastThemeGen;
    if (paramsChanged || themeChanged || !geomValid) {
        buildThemeRamp(palette, 256);
        lastThemeGen = themeGen();
        // Half of ditherAmp()'s figure, because the theme ramps' steps are not
        // evenly spaced and ditherAmp() reports the mean: at full amplitude
        // the shallow gradients here pick up a visible weave instead of losing
        // their contours. The offsets are in sixteenths of a palette index,
        // the units the sum below is carried in.
        const float amp = ditherAmp(palette, 256) * 0.5f;
        for (int k = 0; k < 64; k++) {
            const float d = (static_cast<float>(BAYER8[k]) - 31.5f) * (amp * 16.0f / 31.5f);
            dithOff[k] = static_cast<int16_t>(lroundf(d));
        }

        const int cx = w / 2;
        const int cy = h / 2;
        const int r = (cx < cy ? cx : cy) - 1;
        const int r2 = r * r;
        const int denom = h > 1 ? h - 1 : 1;
        for (int y = 0; y < h; y++) {
            const int dy = y - cy;
            const int inside = r2 - dy * dy;
            discHalfQ4[y] =
                static_cast<int16_t>(inside > 0 ? static_cast<int>(sqrtf(static_cast<float>(inside)) * 16.0f) : -1);
            bgQ4[y] = static_cast<int16_t>((BG_LO * 16) + (y * BG_SPAN * 16) / denom);
        }
        // Horizontal half of the surface shading, plus the dither, in eight
        // copies so the dither can depend on y without band() paying for it.
        const int span = static_cast<int>(p[3]) * SHADE_SPAN / 100;
        const int denomW = w > 1 ? w - 1 : 1;
        for (int ph = 0; ph < 8; ph++) {
            uint16_t *dst = colQ4Ph + static_cast<size_t>(ph) * w;
            const int16_t *off = &dithOff[ph * 8];
            for (int x = 0; x < w; x++) {
                const int v = (x * span * 16) / denomW + off[x & 7];
                dst[x] = static_cast<uint16_t>(v < 0 ? 0 : v);
            }
        }
        geomValid = true;
    }
    memcpy(lastP, p, 4);

    const int cx = w / 2;
    const int cy = h / 2;
    const int denomH = h > 1 ? h - 1 : 1;
    const int base = WEDGE_BASE_LO + static_cast<int>(p[2]) * (WEDGE_BASE_HI - WEDGE_BASE_LO) / 100;
    const int vSpan = static_cast<int>(p[3]) * SHADE_SPAN / 100;
    for (int y = 0; y < h; y++) {
        rowShadeQ4[y] = static_cast<int16_t>(base * 16 + (y * vSpan * 16) / denomH);
    }

    // The wedge. Its width is held between 20 and 140 degrees, which is what
    // keeps the sector convex and so keeps a scanline's intersection with it a
    // single interval.
    const float t = static_cast<float>(tMs) * 0.001f * speedMul(p[0]);
    const float a0 = t * 0.18f;
    const float width = 0.349f + static_cast<float>(p[1]) * (2.443f - 0.349f) / 100.0f;
    const float a1 = a0 + width;
    const float d0x = fastCosRad(a0), d0y = fastSinRad(a0);
    const float d1x = fastCosRad(a1), d1y = fastSinRad(a1);
    // A row's membership in each of the sector's two half planes is a bound on
    // x. The bound is the edge line's own x at this row, a slope times dy, and
    // the slope is the same for every row. A near-horizontal edge has no
    // finite bound, so it degenerates into a whole-row test on the sign.
    const float kEps = 1e-3f;
    const bool h0 = (d0y > kEps) || (d0y < -kEps);
    const bool h1 = (d1y > kEps) || (d1y < -kEps);
    const float s0 = h0 ? (d0x / d0y) : 0.0f;
    const float s1 = h1 ? (d1x / d1y) : 0.0f;

    for (int y = 0; y < h; y++) {
        const int halfQ = discHalfQ4[y];
        if (halfQ < 0) {
            spanLoQ4[y] = 1;
            spanHiQ4[y] = 0; // empty
            continue;
        }
        const int dy = y - cy;
        int loQ = (cx << 4) - halfQ;
        int hiQ = (cx << 4) + halfQ;
        const float fdy = static_cast<float>(dy);
        if (h0) {
            const int bQ = (cx << 4) + static_cast<int>(s0 * fdy * 16.0f);
            if (d0y > 0.0f) {
                if (bQ < hiQ) {
                    hiQ = bQ;
                }
            } else if (bQ > loQ) {
                loQ = bQ;
            }
        } else if (d0x * fdy < 0.0f) {
            loQ = 1;
            hiQ = 0;
        }
        if (h1) {
            const int bQ = (cx << 4) + static_cast<int>(s1 * fdy * 16.0f);
            if (d1y > 0.0f) {
                if (bQ > loQ) {
                    loQ = bQ;
                }
            } else if (bQ < hiQ) {
                hiQ = bQ;
            }
        } else if (-fdy * d1x < 0.0f) {
            loQ = 1;
            hiQ = 0;
        }
        if (loQ < 0) {
            loQ = 0;
        }
        if (hiQ > (w << 4)) {
            hiQ = w << 4;
        }
        if (hiQ <= loQ) {
            spanLoQ4[y] = 1;
            spanHiQ4[y] = 0;
        } else {
            spanLoQ4[y] = static_cast<uint16_t>(loQ);
            spanHiQ4[y] = static_cast<uint16_t>(hiQ);
        }
    }
}

// Portable on every target, so the descriptor's bandRef slot is nullptr, which
// is what BgAnim.h asks for when there is no second path to compare against.
void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int y = y0; y < y0 + rows; y++) {
        uint16_t *row = dst + static_cast<size_t>(y - y0) * w;
        // The background is a gradient of about thirty palette indices over
        // the whole panel, so undithered it steps every sixteen rows and the
        // steps are plain to see. One colour per row cannot carry an ordered
        // dither, so the row carries eight: the same Bayer phase the wedge
        // uses, evaluated once per row into a local array the fill loop
        // indexes with x & 7.
        const int16_t *off = &dithOff[(y & 7) * 8];
        const int bg = bgQ4[y];
        uint16_t bgPat[8];
        for (int k = 0; k < 8; k++) {
            int idx = (bg + off[k]) >> 4;
            if (idx < 0) {
                idx = 0;
            } else if (idx > 255) {
                idx = 255;
            }
            bgPat[k] = palette[idx];
        }
        const int loQ = spanLoQ4[y];
        const int hiQ = spanHiQ4[y];
        if (hiQ <= loQ) {
            for (int x = 0; x < w; x++) {
                row[x] = bgPat[x & 7];
            }
            continue;
        }
        const uint16_t *cq = colQ4Ph + static_cast<size_t>(y & 7) * w;
        const int sh = rowShadeQ4[y];
        int a = loQ >> 4;        // first pixel the run touches
        int b = (hiQ + 15) >> 4; // one past the last
        if (a < 0) {
            a = 0;
        }
        if (b > w) {
            b = w;
        }
        for (int x = 0; x < a; x++) {
            row[x] = bgPat[x & 7];
        }
        for (int x = a; x < b; x++) {
            row[x] = palette[(cq[x] + sh) >> 4];
        }
        for (int x = b; x < w; x++) {
            row[x] = bgPat[x & 7];
        }
        // The two end pixels by coverage, so a straight edge does not step as
        // it turns. Written as a loop over the two so the case where one pixel
        // carries both edges falls out of the same arithmetic.
        for (int k = 0; k < 2; k++) {
            const int x = k == 0 ? a : b - 1;
            if (x < 0 || x >= w || x < a || x >= b) {
                continue;
            }
            const int pxLo = x << 4;
            const int pxHi = pxLo + 16;
            const int covLo = loQ > pxLo ? loQ : pxLo;
            const int covHi = hiQ < pxHi ? hiQ : pxHi;
            const int cov = covHi - covLo;
            if (cov >= 16 || cov <= 0) {
                continue;
            }
            row[x] = blendQ8(bgPat[x & 7], palette[(cq[x] + sh) >> 4], cov * 16);
        }
    }
}

void release() {
    releaseTable(discHalfQ4, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(spanLoQ4, static_cast<size_t>(allocH) * sizeof(uint16_t));
    releaseTable(spanHiQ4, static_cast<size_t>(allocH) * sizeof(uint16_t));
    releaseTable(bgQ4, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(colQ4Ph, static_cast<size_t>(8 * allocW) * sizeof(uint16_t));
    releaseTable(rowShadeQ4, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(palette, 256 * sizeof(uint16_t));
    allocW = allocH = 0;
    lastThemeGen = 0xFFFFFFFF;
    geomValid = false;
    lastP[0] = lastP[1] = lastP[2] = lastP[3] = 255;
}

} // namespace

// Index bound, which is why the wedge path needs no clamp. colQ4Ph holds
// (x * span * 16) / (w - 1) with span at most SHADE_SPAN, so at most 800, plus
// a dither offset ditherAmp() itself caps at 16.0f and this animation halves,
// so at most 128; the store clamps the low side at 0. rowShadeQ4 holds
// (base + vertical) * 16 with base at most WEDGE_BASE_HI and vertical at most
// SHADE_SPAN, so 640 to 2,880. The sum is 640 to 3,808, which is 40 to 238
// after the shift.
extern const BgAnimation bg_anim_sundial;
const BgAnimation bg_anim_sundial = {
    "sundial",
    "Sundial",
    {{"speed", "Speed", 10},
     {"width", "Wedge width", 40},
     {"contrast", "Contrast", 25},
     {"shading", "Surface shading", 30}},
    init,
    frame,
    band,
    release,
    nullptr,
};

#endif // GAGGIMATE_SIM
