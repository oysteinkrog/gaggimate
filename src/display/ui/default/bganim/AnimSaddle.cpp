#ifndef GAGGIMATE_SIM

// "Saddle" - two broad regions brightening toward opposite edges, the regions
// between them subdued, with curved tonal boundaries drifting through a quiet
// centre. Brainstormed 2026-09-09 with GPT (Codex CLI, gpt-6-astra) in the
// replacement round of gm-4bd.
//
// The index combine is a multiply rather than an add, which is the whole idea:
//
//   index = (colTermPh[x] * rowTerm[y] + 131072) >> 10
//
// Two gathers, one multiply, one add, one shift. The level sets of a product
// of two one-dimensional profiles are hyperbolas, so the tonal boundaries are
// curved even though both tables are straight lines of numbers. What is not
// curved is the zero set, which is one vertical and one horizontal line; that
// is why each profile has exactly ONE crossing and a wide, gentle shoulder
// around it. The crossing then reads as a broad quiet band through the middle
// rather than as a drawn cross, and there are four regions, not a
// checkerboard.
//
// The bias of 131072 is 128 << 10, so the expression is exactly
// 128 + ((c * r) >> 10) without ever right-shifting a negative value. Both
// forms round the same way, toward minus infinity, so this is a rewrite and
// not an approximation; it just keeps the kernel clear of a shift whose
// behaviour on negative operands C++17 leaves to the implementation.
//
// The dither folded into the column copies is multiplied by the row value
// along with everything else, and that is the right behaviour rather than a
// compromise. A row with a small row value spans a small part of the palette,
// so its gradient is shallow, its contours are far apart and it needs
// proportionally less dither, which is exactly what the multiply gives it.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <string.h>

namespace {
using namespace bganim;

// Ranges. The product of the two caps is 129,032, which the shift takes to
// 126, so the index lands in 1..254 and needs no clamp. The column term
// carries eight times the resolution of the row term so the dither, which
// lives in column units, still has somewhere to go.
constexpr int COL_MAX = 1016;
constexpr int ROW_MAX = 127;
constexpr int BIAS = 128 << 10;

int16_t *colTerm = nullptr;   // frame() only -> PSRAM
int16_t *colTermPh = nullptr; // read every pixel -> slab, 8 y-phase copies
int16_t *rowTerm = nullptr;   // read once per row -> slab
uint16_t *themeRamp = nullptr; // frame() only -> PSRAM
uint16_t *palette = nullptr;  // read every pixel -> slab

int16_t dithOff[64] = {0};

uint8_t lastP[4] = {255, 255, 255, 255};
uint32_t lastThemeGen = 0xFFFFFFFF;
int allocW = 0, allocH = 0;

// Table placement, against the 9,216 B per-animation slab:
//   colTermPh  7,680 B  read every pixel                   HOT
//   rowTerm      960 B  read once per row                  HOT
//   palette      512 B  read every pixel, data-dependent   HOT
//   -------------------------------------------------------------
//              9,152 B of 9,216 B
//   colTerm      960 B  read only in frame()               PSRAM
//   themeRamp    512 B  read only when the palette is
//                       rebuilt                            PSRAM
void release();

bool init(int w, int h) {
    if (sinLut() == nullptr) {
        return false;
    }
    if (colTerm == nullptr) {
        colTerm = static_cast<int16_t *>(alloc(w * sizeof(int16_t)));
        if (colTerm == nullptr) {
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

// A smooth odd shoulder, 1.5u - 0.5u^3 in Q8: -256 at u = -256, 0 at 0, 256 at
// 256, and flat at both ends so the bright regions do not keep climbing all
// the way to the panel edge.
int shoulderQ8(int u) {
    if (u > 256) {
        u = 256;
    }
    if (u < -256) {
        u = -256;
    }
    const int uu = (u * u) / 256; // u * u is non-negative, but keep the form uniform
    return (u * (768 - uu)) / 512;
}

void buildPalette(int contrastP) {
    buildThemeRamp(themeRamp, 256);
    // A monotone ramp, so one pair of opposite regions is bright and the other
    // pair dark, with the quiet band in between. The contrast slider bends it
    // into an S, which widens the quiet band and pushes the bright regions
    // further up the theme.
    const int s = contrastP; // 0..100, how much S to apply
    for (int i = 0; i < 256; i++) {
        const int lin = i;
        const int u = i - 128;
        const int curved = 128 + shoulderQ8(u * 2) / 2; // same shoulder, reused
        int v = (lin * (100 - s) + curved * s) / 100;
        v = 10 + (v * 235) / 255;
        palette[i] = themeRamp[v < 0 ? 0 : (v > 255 ? 255 : v)];
    }
    // The dither lives in column units, where eight units are one palette index
    // at the largest row value. Half the mean spacing, because the palette's
    // steep middle would otherwise show the pattern.
    const float amp = ditherAmp(palette, 256) * 0.5f;
    for (int k = 0; k < 64; k++) {
        const float d = (static_cast<float>(BAYER8[k]) - 31.5f) * (amp * 8.0f / 31.5f);
        dithOff[k] = static_cast<int16_t>(lroundf(d));
    }
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[BG_ANIM_PARAMS]) {
    if (memcmp(p, lastP, 4) != 0 || themeGen() != lastThemeGen) {
        memcpy(lastP, p, 4);
        lastThemeGen = themeGen();
        buildPalette(p[3]);
    }

    // Time. Unsigned, so the millis() wrap is a phase wrap. The two crossings
    // drift at rates in an awkward ratio, so the saddle never returns to the
    // same place on a beat you can follow.
    const uint32_t sp = 4 + static_cast<uint32_t>(p[0]) * 44 / 100; // 0.25x..3x
    const uint32_t ph = (tMs * sp) >> 12;
    const int16_t *sl = sinLut();

    // How far the crossings wander from the centre, as a fraction of the panel.
    const int driftW = w * static_cast<int>(p[2]) / 400; // up to a quarter of the width
    const int driftH = h * static_cast<int>(p[2]) / 400;
    // Divisions rather than shifts wherever the value can be negative: a
    // right shift of a negative operand is implementation-defined in C++17 and
    // none of this is per pixel, so there is nothing to buy by relying on it.
    const int x0 = w / 2 + (sl[ph & (SIN_N - 1)] * driftW) / 512;
    const int y0 = h / 2 + (sl[((ph * 7) >> 3) & (SIN_N - 1)] * driftH) / 512;

    // The shoulder half-width. A narrow crossing bends the hyperbolas tightly,
    // a wide one leaves a broad quiet band, which is what the slider is for.
    const int hwW = w / 4 + w * static_cast<int>(p[1]) / 200; // w/4 .. 3w/4
    const int hwH = h / 4 + h * static_cast<int>(p[1]) / 200;

    for (int y = 0; y < h; y++) {
        const int u = ((y - y0) * 256) / (hwH > 0 ? hwH : 1);
        rowTerm[y] = static_cast<int16_t>(shoulderQ8(u) * ROW_MAX / 256);
    }
    for (int x = 0; x < w; x++) {
        const int u = ((x - x0) * 256) / (hwW > 0 ? hwW : 1);
        colTerm[x] = static_cast<int16_t>(shoulderQ8(u) * COL_MAX / 256);
    }
    for (int ph8 = 0; ph8 < 8; ph8++) {
        int16_t *dstPh = colTermPh + static_cast<size_t>(ph8) * w;
        const int16_t *off = &dithOff[ph8 * 8];
        for (int x = 0; x < w; x++) {
            int v = colTerm[x] + off[x & 7];
            v = v < -COL_MAX ? -COL_MAX : (v > COL_MAX ? COL_MAX : v);
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
            const uint16_t c0 = palette[(ct[x] * rt + BIAS) >> 10];
            const uint16_t c1 = palette[(ct[x + 1] * rt + BIAS) >> 10];
            *reinterpret_cast<uint32_t *>(dst) = static_cast<uint32_t>(c0) | (static_cast<uint32_t>(c1) << 16);
            dst += 2;
        }
        for (; x < w; x++) {
            *dst++ = palette[(ct[x] * rt + BIAS) >> 10];
        }
    }
}

void release() {
    releaseTable(colTerm, static_cast<size_t>(allocW) * sizeof(int16_t));
    releaseTable(colTermPh, static_cast<size_t>(8 * allocW) * sizeof(int16_t));
    releaseTable(rowTerm, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(themeRamp, 256 * sizeof(uint16_t));
    releaseTable(palette, 256 * sizeof(uint16_t));
    allocW = allocH = 0;
    lastThemeGen = 0xFFFFFFFF;
    lastP[0] = lastP[1] = lastP[2] = lastP[3] = 255;
}

} // namespace

extern const BgAnimation bg_anim_saddle;
const BgAnimation bg_anim_saddle = {
    "saddle",
    "Saddle",
    {{"speed", "Speed", 12}, {"curvature", "Curvature", 35}, {"drift", "Drift", 25}, {"contrast", "Contrast", 30}},
    init,
    frame,
    band,
    release,
    nullptr,
};

#endif // GAGGIMATE_SIM
