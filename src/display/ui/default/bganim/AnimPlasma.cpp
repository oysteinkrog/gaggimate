#ifndef GAGGIMATE_SIM

// "Plasma" — the original sleep animation: palette-cycled classic plasma in
// espresso tones. Two per-column + two per-row sine terms summed per pixel,
// indexed into a rotating 256-entry palette.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <string.h>

namespace {
using namespace bganim;

int16_t *colTerm = nullptr;
// colTerm plus the ordered dither, in four copies -- one per y&3 Bayer phase.
// The dither has to vary with both x&3 and y&3, and rowTerm[y] is a single
// scalar per row, so there is nowhere else to hide it: folding it here keeps
// band()'s inner loop byte-identical to the undithered version, at the cost of
// 4*w int16 stores per frame in frame() (1920 at w=480, against 230k pixels).
int16_t *colTermPh = nullptr;
int16_t *rowTerm = nullptr;
// Dither offsets in PRE-SHIFT units, where one palette index is 16, so a
// sub-index amplitude survives the >>4 in band(). Rebuilt with the wheel.
int16_t dithOff[16] = {0};
uint16_t *palette = nullptr;    // base palette (theme-cycled build)
uint16_t *rotPalette = nullptr; // palette pre-rotated by `cycle` each frame,
                                // so band() can index with a plain & 255
                                // instead of an extra per-pixel "+ cycle".
uint8_t lastP[4] = {255, 255, 255, 255}; // force first palette build
uint32_t lastThemeGen = 0xFFFFFFFF;
// Dimensions colTerm/rowTerm were sized for. release() runs after a
// resolution change too, when w/h no longer describe the allocation.
int allocW = 0, allocH = 0;
uint32_t phase1 = 0;
uint32_t phase2 = 0;
uint32_t phase3 = 0;
uint32_t cycle = 0;

bool init(int w, int h) {
    if (sinLut() == nullptr) {
        return false;
    }
    if (colTerm == nullptr) {
        colTerm = static_cast<int16_t *>(alloc(w * sizeof(int16_t)));
        allocW = w;
    }
    if (colTermPh == nullptr) {
        // Sized from allocW, not w, so it always matches what colTerm can hold
        // and what release() frees: if a retried init() sees colTerm already
        // allocated at an earlier width, allocW is that earlier width.
        colTermPh = static_cast<int16_t *>(alloc(4 * allocW * sizeof(int16_t)));
    }
    if (rowTerm == nullptr) {
        rowTerm = static_cast<int16_t *>(alloc(h * sizeof(int16_t)));
        allocH = h;
    }
    if (palette == nullptr) {
        palette = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
    }
    if (rotPalette == nullptr) {
        rotPalette = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
    }
    return colTerm != nullptr && colTermPh != nullptr && rowTerm != nullptr && palette != nullptr &&
           rotPalette != nullptr;
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[4]) {
    if (memcmp(p, lastP, 4) != 0 || themeGen() != lastThemeGen) {
        memcpy(lastP, p, 4);
        lastThemeGen = themeGen();
        const uint16_t bright = 64 + static_cast<uint16_t>(p[2]) * 192 / 100; // 25%..100%
        buildThemeWheel(palette, bright);
        // Plasma had no dither at all, and it was the worst bander in the fleet:
        // 35.6% of disc pixels on a monotone <=1 LSB staircase at brightness
        // 100, 46.0% at 55. The 0.75 is because ditherAmp() returns the MEAN
        // step spacing, and a wheel's gradient is not uniform -- the steep arcs
        // would get over-dithered into visible texture at full amplitude. At
        // 0.75 the contour share is 12.6%/9.4% with the pattern still hidden.
        const float amp = ditherAmp(palette, 256) * 0.75f;
        for (int k = 0; k < 16; k++) {
            const float d = (static_cast<float>(BAYER4[k]) - 7.5f) * (amp * 16.0f / 7.5f);
            dithOff[k] = static_cast<int16_t>(lroundf(d));
        }
    }
    // Speed 0-100 -> 0.25x..3x of the original drift (which advanced ~60
    // sine-index units per second on the fastest term).
    const uint32_t speedMul = 4 + static_cast<uint32_t>(p[0]) * 44 / 100; // 4..48, /16 = 0.25..3
    const uint32_t base = tMs * speedMul >> 4;                            // ~= original frame*2 at 50
    phase1 = base * 30 >> 9; // ratios preserved from the frame-based original
    phase2 = base * 23 >> 9;
    phase3 = base * 26 >> 9;
    cycle = base * 11 >> 9;

    // Scale 0-100 -> 0.5x..2x spatial frequency.
    const uint32_t sx = 128 + static_cast<uint32_t>(p[1]) * 384 / 100; // 128..512, /256
    const uint32_t f5 = (5 * sx) >> 8;
    const uint32_t f2 = (2 * sx) >> 8; // sx >= 128 keeps every frequency >= 1
    const uint32_t f4 = (4 * sx) >> 8;
    const uint32_t f3 = (3 * sx) >> 8;
    // Hoist the LUT pointer: sin1024() re-calls sinLut() (a real call8 on
    // Xtensa — the lazy-init check inside it defeats cross-TU inlining) on
    // every use, which otherwise costs 4 calls x (w+h) per frame here.
    const int16_t *sl = sinLut();
    for (int x = 0; x < w; x++) {
        colTerm[x] = sl[(x * f5 + phase1) & (SIN_N - 1)] + sl[(x * f2 + SIN_N - (phase2 & (SIN_N - 1))) & (SIN_N - 1)];
    }
    for (int y = 0; y < h; y++) {
        rowTerm[y] = sl[(y * f4 + phase2) & (SIN_N - 1)] + sl[(y * f3 + phase3) & (SIN_N - 1)];
    }
    // Expand into the four y-phase copies. colTerm's own range is +-1024 (two
    // sine terms of amplitude 512) and the dither adds at most +-256, so int16
    // still has headroom.
    for (int ph = 0; ph < 4; ph++) {
        int16_t *dstPh = colTermPh + static_cast<size_t>(ph) * w;
        const int16_t *off = &dithOff[ph * 4];
        for (int x = 0; x < w; x++) {
            dstPh[x] = static_cast<int16_t>(colTerm[x] + off[x & 3]);
        }
    }

    // Pre-rotate the palette by `cycle` once per frame so band() can index
    // with a plain `& 255` instead of paying a per-pixel "+ cycle" add.
    // rotPalette[i] == palette[(i + cycle) & 255] for all i in 0..255, which
    // is algebraically identical to the old per-pixel ((v>>4) + cycle) & 255
    // since (v>>4) & 255 already reduces v>>4 mod 256 before the rotation.
    const uint32_t rot = cycle & 255;
    for (int i = 0; i < 256; i++) {
        rotPalette[i] = palette[(i + rot) & 255];
    }
}

void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int y = y0; y < y0 + rows; y++) {
        const int rt = rowTerm[y];
        // The dither is already in this row's phase copy, so the loop below is
        // unchanged from the undithered version -- zero per-pixel cost.
        const int16_t *ct = colTermPh + static_cast<size_t>(y & 3) * w;
        int x = 0;
        // Emit pixels in pairs via a single uint32 store where possible —
        // halves the number of store instructions in the hot loop (device
        // has no unaligned-16 penalty here since dst is always 32-bit
        // aligned: bands start at a row boundary and w is even (480)).
        for (; x + 1 < w; x += 2) {
            const uint16_t p0 = rotPalette[((ct[x] + rt) >> 4) & 255];
            const uint16_t p1 = rotPalette[((ct[x + 1] + rt) >> 4) & 255];
            *reinterpret_cast<uint32_t *>(dst) = static_cast<uint32_t>(p0) | (static_cast<uint32_t>(p1) << 16);
            dst += 2;
        }
        for (; x < w; x++) {
            *dst++ = rotPalette[((ct[x] + rt) >> 4) & 255];
        }
    }
}

void release() {
    releaseTable(colTerm, static_cast<size_t>(allocW) * sizeof(int16_t));
    releaseTable(colTermPh, static_cast<size_t>(4 * allocW) * sizeof(int16_t));
    releaseTable(rowTerm, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(palette, 256 * sizeof(uint16_t));
    releaseTable(rotPalette, 256 * sizeof(uint16_t));
    allocW = allocH = 0;
    lastThemeGen = 0xFFFFFFFF;
}

} // namespace

// extern: const namespace-scope objects default to internal linkage.
extern const BgAnimation bg_anim_plasma;
const BgAnimation bg_anim_plasma = {
    "plasma",
    "Plasma",
    {{"speed", "Speed", 50}, {"scale", "Scale", 50}, {"brightness", "Brightness", 70}, {nullptr, nullptr, 0}},
    init,
    frame,
    band,
    release,
};

#endif // GAGGIMATE_SIM
