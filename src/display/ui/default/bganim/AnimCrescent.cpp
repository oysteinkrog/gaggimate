#ifndef GAGGIMATE_SIM

// "Matte Crescent" - a pale crescent floating on a dark face, turning slowly
// and swelling and thinning as it turns. Matte rather than glowing: the
// crescent carries a shallow diagonal shading and no rim light, so it reads as
// a lit surface rather than a lamp. Brainstormed 2026-09-09 with GPT (Codex
// CLI, gpt-6-astra) as candidate 8 of gm-4bd.
//
// The second run based renderer, and the one that shows why the shape matters.
// A crescent is one disc with an equal disc cut out of it, offset. That is not
// convex, so a scanline can meet it in two intervals, but never in three: the
// row's outer chord minus the row's inner chord leaves at most a piece at each
// end. So frame() stores four endpoints per row and band() walks at most two
// runs. Inside a run the expression is the separable one,
//
//   palette[(colQ4Ph[(y & 7) * w + x] + rowShadeQ4[y]) >> 4]
//
// two gathers, one add and one shift, with the ordered dither already folded
// into the eight y-phase copies. Outside the runs a row is an eight pixel
// repeat of its own background colour, and the run walk is arranged so every
// pixel of the row is written exactly once.
//
// Both discs have the same radius, which is what makes the cut a crescent with
// sharp tips rather than a ring segment, and it also means one table of chord
// half-widths serves both. The table is built once per size, so no square root
// runs per frame. The inner disc's centre moves continuously, so its row is
// sampled from the table with a linear interpolation; without that the inner
// edge would step a whole pixel at a time as the crescent turns.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>
#include <string.h>

namespace {
using namespace bganim;

// Palette index budget for a crescent pixel: rowShadeQ4 carries the base level
// plus the vertical half of the shading, colQ4Ph the horizontal half plus the
// dither. The ranges are chosen so the sum stays inside 0..255 at every slider
// position, which is why the run loop needs no clamp.
constexpr int CRES_BASE_LO = 60;
constexpr int CRES_BASE_HI = 170;
constexpr int SHADE_SPAN = 36; // per axis, palette indices
constexpr int BG_LO = 6;       // background gradient, in theme ramp indices
constexpr int BG_SPAN = 18;

uint16_t *chordQ4 = nullptr;   // half a disc's chord, by |dy| -> PSRAM
uint16_t *runQ4 = nullptr;     // four endpoints per row -> PSRAM
int16_t *bgQ4 = nullptr;       // read once per row -> PSRAM
uint16_t *colQ4Ph = nullptr;   // read every crescent pixel -> slab
int16_t *rowShadeQ4 = nullptr; // the add on every crescent pixel -> slab
uint16_t *palette = nullptr;   // read every crescent pixel -> slab

int16_t dithOff[64] = {0};

uint8_t lastP[4] = {255, 255, 255, 255};
uint32_t lastThemeGen = 0xFFFFFFFF;
bool geomValid = false;
int allocW = 0, allocH = 0, allocChord = 0;
int discR = 0;

// Table placement, against the 9,216 B per-animation slab:
//   colQ4Ph    7,680 B  read every crescent pixel          HOT
//   rowShadeQ4   960 B  the add on every crescent pixel    HOT
//   palette      512 B  read every crescent pixel          HOT
//   -------------------------------------------------------------
//              9,152 B of 9,216 B
//   runQ4      3,840 B  four reads per row                 PSRAM
//   bgQ4         960 B  one read per row                   PSRAM
//   chordQ4      482 B  two reads per row, in frame() only PSRAM
// runQ4 interleaves the four endpoints of a row, so a row's four reads are one
// eight byte group and a band's reads run forward through memory.
void release();

bool init(int w, int h) {
    if (sinLut() == nullptr) {
        return false;
    }
    const int cx = w / 2, cy = h / 2;
    const int rMax = (cx < cy ? cx : cy);
    // Every early return goes through release(), so a half built set is never
    // left behind for the retry to trip over (gm-bzu.15).
    if (chordQ4 == nullptr) {
        chordQ4 = static_cast<uint16_t *>(alloc((rMax + 2) * sizeof(uint16_t)));
        if (chordQ4 == nullptr) {
            release();
            return false;
        }
        allocChord = rMax + 2;
    }
    if (runQ4 == nullptr) {
        runQ4 = static_cast<uint16_t *>(alloc(4 * h * sizeof(uint16_t)));
        if (runQ4 == nullptr) {
            release();
            return false;
        }
        allocH = h;
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

void frame(uint32_t tMs, int w, int h, const uint8_t p[4]) {
    const int cx = w / 2;
    const int cy = h / 2;
    const bool paramsChanged = memcmp(p, lastP, 4) != 0;
    const bool themeChanged = themeGen() != lastThemeGen;
    if (paramsChanged || themeChanged || !geomValid) {
        buildThemeRamp(palette, 256);
        lastThemeGen = themeGen();
        // Half of ditherAmp()'s figure: it reports the mean step of a ramp
        // whose steps are not evenly spaced, and the gradients here are
        // shallow enough that the full amplitude shows as a weave rather than
        // dissolving a contour. Offsets are in sixteenths of a palette index.
        const float amp = ditherAmp(palette, 256) * 0.5f;
        for (int k = 0; k < 64; k++) {
            const float d = (static_cast<float>(BAYER8[k]) - 31.5f) * (amp * 16.0f / 31.5f);
            dithOff[k] = static_cast<int16_t>(lroundf(d));
        }

        const int rMax = (cx < cy ? cx : cy);
        discR = rMax * (40 + static_cast<int>(p[1]) * 55 / 100) / 100;
        if (discR < 8) {
            discR = 8;
        }
        for (int k = 0; k < allocChord; k++) {
            const int inside = discR * discR - k * k;
            chordQ4[k] = static_cast<uint16_t>(inside > 0 ? static_cast<int>(sqrtf(static_cast<float>(inside)) * 16.0f)
                                                          : 0);
        }
        const int denom = h > 1 ? h - 1 : 1;
        for (int y = 0; y < h; y++) {
            bgQ4[y] = static_cast<int16_t>((BG_LO * 16) + (y * BG_SPAN * 16) / denom);
        }
        // Horizontal half of the surface shading, plus the dither, in eight
        // copies so the dither can depend on y without band() paying for it.
        const int denomW = w > 1 ? w - 1 : 1;
        for (int ph = 0; ph < 8; ph++) {
            uint16_t *dst = colQ4Ph + static_cast<size_t>(ph) * w;
            const int16_t *off = &dithOff[ph * 8];
            for (int x = 0; x < w; x++) {
                const int v = (x * SHADE_SPAN * 16) / denomW + off[x & 7];
                dst[x] = static_cast<uint16_t>(v < 0 ? 0 : v);
            }
        }
        const int base = CRES_BASE_LO + static_cast<int>(p[3]) * (CRES_BASE_HI - CRES_BASE_LO) / 100;
        for (int y = 0; y < h; y++) {
            rowShadeQ4[y] = static_cast<int16_t>(base * 16 + (y * SHADE_SPAN * 16) / denom);
        }
        geomValid = true;
    }
    memcpy(lastP, p, 4);

    // The cut disc's centre: a slow turn, and a slower swell that takes the
    // offset from a thin crescent to a fat one and back.
    const float t = static_cast<float>(tMs) * 0.001f * speedMul(p[0]);
    const float ang = t * 0.21f;
    const float swell = 0.5f - 0.5f * fastCosRad(t * 0.11f);
    const float dMin = static_cast<float>(discR) * 0.12f;
    const float dMax = static_cast<float>(discR) * (0.25f + static_cast<float>(p[2]) * 0.60f / 100.0f);
    const float d = dMin + (dMax - dMin) * swell;
    const int icxQ4 = (cx << 4) + static_cast<int>(d * fastCosRad(ang) * 16.0f);
    const int icyQ4 = (cy << 4) + static_cast<int>(d * fastSinRad(ang) * 16.0f);

    const int wQ4 = w << 4;
    for (int y = 0; y < h; y++) {
        uint16_t *rec = runQ4 + static_cast<size_t>(y) * 4;
        int dy = y - cy;
        if (dy < 0) {
            dy = -dy;
        }
        if (dy > discR) {
            rec[0] = 1;
            rec[1] = 0;
            rec[2] = 1;
            rec[3] = 0;
            continue;
        }
        int o0 = (cx << 4) - chordQ4[dy];
        int o1 = (cx << 4) + chordQ4[dy];
        if (o0 < 0) {
            o0 = 0;
        }
        if (o1 > wQ4) {
            o1 = wQ4;
        }
        // The cut disc's chord at this row. Its centre moves in sixteenths, so
        // the table is read at the two neighbouring integer distances and
        // interpolated; rounding the distance instead makes the inner edge
        // step a whole pixel as the crescent turns.
        int kQ4 = (y << 4) - icyQ4;
        if (kQ4 < 0) {
            kQ4 = -kQ4;
        }
        const int ki = kQ4 >> 4;
        int i0 = 1, i1 = 0;
        if (ki < allocChord - 1) {
            const int f = kQ4 & 15;
            const int c0 = chordQ4[ki];
            const int c1 = chordQ4[ki + 1];
            const int half = c0 + ((c1 - c0) * f >> 4);
            if (half > 0) {
                i0 = icxQ4 - half;
                i1 = icxQ4 + half;
            }
        }
        if (i1 <= i0 || i1 <= o0 || i0 >= o1) {
            // No cut on this row: one run, the whole outer chord.
            rec[0] = static_cast<uint16_t>(o0);
            rec[1] = static_cast<uint16_t>(o1 > o0 ? o1 : o0);
            rec[2] = 1;
            rec[3] = 0;
            continue;
        }
        const int aHi = i0 < o1 ? i0 : o1;
        const int bLo = i1 > o0 ? i1 : o0;
        if (aHi > o0) {
            rec[0] = static_cast<uint16_t>(o0);
            rec[1] = static_cast<uint16_t>(aHi);
        } else {
            rec[0] = 1;
            rec[1] = 0;
        }
        if (o1 > bLo) {
            rec[2] = static_cast<uint16_t>(bLo);
            rec[3] = static_cast<uint16_t>(o1);
        } else {
            rec[2] = 1;
            rec[3] = 0;
        }
    }
}

// Portable on every target, so the descriptor's bandRef slot is nullptr, which
// is what BgAnim.h asks for when there is no second path to compare against.
void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int y = y0; y < y0 + rows; y++) {
        uint16_t *row = dst + static_cast<size_t>(y - y0) * w;
        // The background gradient spans about eighteen palette indices over
        // the whole panel, so undithered it steps every twenty-odd rows and
        // the steps are plain to see. One colour per row cannot carry an
        // ordered dither, so the row carries eight, built once per row from
        // the same Bayer phase the crescent uses.
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
        const uint16_t *rec = runQ4 + static_cast<size_t>(y) * 4;
        const uint16_t *cq = colQ4Ph + static_cast<size_t>(y & 7) * w;
        const int sh = rowShadeQ4[y];
        int cur = 0;
        for (int k = 0; k < 2; k++) {
            const int loQ = rec[k * 2];
            const int hiQ = rec[k * 2 + 1];
            if (hiQ <= loQ) {
                continue;
            }
            int a = loQ >> 4;        // first pixel the run touches
            int b = (hiQ + 15) >> 4; // one past the last
            if (a < cur) {
                a = cur;
            }
            if (b > w) {
                b = w;
            }
            if (b <= a) {
                continue;
            }
            for (int x = cur; x < a; x++) {
                row[x] = bgPat[x & 7];
            }
            for (int x = a; x < b; x++) {
                row[x] = palette[(cq[x] + sh) >> 4];
            }
            // The two end pixels by coverage, so the crescent's edges do not
            // step as it turns. Two blends per run, paid per row.
            for (int e = 0; e < 2; e++) {
                const int x = e == 0 ? a : b - 1;
                if (x < a || x >= b) {
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
            cur = b;
        }
        for (int x = cur; x < w; x++) {
            row[x] = bgPat[x & 7];
        }
    }
}

void release() {
    releaseTable(chordQ4, static_cast<size_t>(allocChord) * sizeof(uint16_t));
    releaseTable(runQ4, static_cast<size_t>(4 * allocH) * sizeof(uint16_t));
    releaseTable(bgQ4, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(colQ4Ph, static_cast<size_t>(8 * allocW) * sizeof(uint16_t));
    releaseTable(rowShadeQ4, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(palette, 256 * sizeof(uint16_t));
    allocW = allocH = allocChord = 0;
    discR = 0;
    lastThemeGen = 0xFFFFFFFF;
    geomValid = false;
    lastP[0] = lastP[1] = lastP[2] = lastP[3] = 255;
}

} // namespace

// Index bound, which is why the run loop needs no clamp. colQ4Ph holds
// (x * SHADE_SPAN * 16) / (w - 1), so at most 576, plus a dither offset
// ditherAmp() itself caps at 16.0f and this animation halves, so at most 128;
// the store clamps the low side at 0. rowShadeQ4 holds (base + vertical) * 16
// with base at most CRES_BASE_HI, so 960 to 3,296. The sum is 960 to 4,000,
// which is 60 to 250 after the shift.
extern const BgAnimation bg_anim_crescent;
const BgAnimation bg_anim_crescent = {
    "crescent",
    "Matte Crescent",
    {{"speed", "Speed", 15},
     {"size", "Size", 70},
     {"phase", "Phase range", 40},
     {"contrast", "Contrast", 40}},
    init,
    frame,
    band,
    release,
    nullptr,
};

#endif // GAGGIMATE_SIM
