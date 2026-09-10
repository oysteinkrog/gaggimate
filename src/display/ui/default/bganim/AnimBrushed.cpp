#ifndef GAGGIMATE_SIM

// "Brushed Metal" - fine horizontal grain with one broad reflection sliding
// across it. Brainstormed 2026-09-09 with GPT (Codex CLI, gpt-6-astra) as
// candidate 1 of gm-4bd.
//
// The kernel is Plasma's: one per-column table with the 8x8 Bayer dither
// already folded into eight y-phase copies, one per-row scalar, an add, a
// shift and one palette gather. What makes it read as metal rather than as
// plasma is where the two terms get their content. The row term carries a
// fixed-seed grain, so every row sits at its own slightly different level and
// the field reads as horizontal brushing; the column term carries two wide,
// slow lobes, which is the reflection. The grain's seed is fixed at init, so
// a row's grain is the same every frame and nothing crawls: only the
// reflection's phase and a very slow vertical breathe move.
//
// Index safety. band() computes (colTermPh[x] + rowTerm[y]) >> 4 and gathers
// with it, with no per-pixel bound check. frame() guarantees the sum lands in
// 0..4095 by construction: rowTerm is built first, its largest magnitude is
// measured, and colTermPh is then clamped into [rowMax, 4095 - rowMax]. That
// holds at every parameter combination, which is what the fuzz harness checks
// with the sanitizers on.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <string.h>

namespace {
using namespace bganim;

constexpr int INDEX_MAX = 4095; // (colTermPh + rowTerm) must stay in 0..INDEX_MAX

int16_t *colTerm = nullptr;   // frame() only -> PSRAM
int16_t *colTermPh = nullptr; // read every pixel -> slab, 8 y-phase copies
int16_t *rowTerm = nullptr;   // read once per row -> slab
int16_t *grainBase = nullptr; // read once per row in frame() -> PSRAM
uint16_t *palette = nullptr;  // read every pixel -> slab

// Dither offsets in PRE-SHIFT units, where one palette index is 16, so a
// sub-index amplitude survives band()'s >>4. Rebuilt with the palette.
int16_t dithOff[64] = {0};

uint8_t lastP[4] = {255, 255, 255, 255};
uint32_t lastThemeGen = 0xFFFFFFFF;
int allocW = 0, allocH = 0;

// Table placement, against the 9,216 B per-animation slab:
//   colTermPh  7,680 B  read every pixel                       HOT
//   rowTerm      960 B  read once per row, and on the address
//                       path to every pixel of that row        HOT
//   palette      512 B  read every pixel, by a data-dependent
//                       index, which is the access a PSRAM
//                       miss punishes hardest                  HOT
//   ----------------------------------------------------------------
//              9,152 B of 9,216 B
//   colTerm      960 B  read only in frame()                   PSRAM
//   grainBase    960 B  read once per row in frame(), a
//                       sequential sweep                       PSRAM
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
        // Sized from allocW, not w, so a retried init() that finds colTerm
        // already allocated at an earlier width agrees with what release()
        // will free.
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
    if (grainBase == nullptr) {
        grainBase = static_cast<int16_t *>(alloc(allocH * sizeof(int16_t)));
        if (grainBase == nullptr) {
            release();
            return false;
        }
        // Fixed seed: the grain is a property of the surface, not of time. Two
        // scales, so the streaks vary in width the way a brushed finish does:
        // a coarse level held for two to six rows, plus a fine per-row jitter.
        uint32_t s = 0x9E3779B9u;
        int coarse = 0, hold = 0;
        for (int y = 0; y < allocH; y++) {
            if (hold == 0) {
                coarse = static_cast<int>(nextRand(s) & 1023) - 512;
                hold = 2 + static_cast<int>(nextRand(s) % 5u);
            }
            hold--;
            const int fine = static_cast<int>(nextRand(s) & 1023) - 512;
            grainBase[y] = static_cast<int16_t>((coarse * 5 + fine * 3) >> 3);
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
    if (memcmp(p, lastP, 4) != 0 || themeGen() != lastThemeGen) {
        memcpy(lastP, p, 4);
        lastThemeGen = themeGen();
        buildThemeRamp(palette, 256);
        // A plain ramp, so the whole dither amplitude is wanted: unlike
        // Plasma's wheel there are no steep arcs here to over-dither.
        const float amp = ditherAmp(palette, 256);
        for (int k = 0; k < 64; k++) {
            const float d = (static_cast<float>(BAYER8[k]) - 31.5f) * (amp * 16.0f / 31.5f);
            dithOff[k] = static_cast<int16_t>(lroundf(d));
        }
    }

    // Time. Everything below is unsigned, so the wrap at 4.29e9 ms is a phase
    // wrap and nothing else; the multiply is allowed to overflow for the same
    // reason (Plasma does the same).
    const uint32_t sp = 4 + static_cast<uint32_t>(p[0]) * 44 / 100; // 0.25x..3x
    const uint32_t base = (tMs * sp) >> 4;
    const uint32_t ph1 = base >> 5;         // the wide reflection, slowest
    const uint32_t ph2 = (base * 3) >> 7;   // the narrower one, drifting apart
    const uint32_t ph3 = base >> 7;         // the vertical breathe

    const int16_t *sl = sinLut();

    // Row term: the grain, plus a very slow vertical breathe so the surface is
    // not completely static when the reflection is off to one side.
    const int grainQ8 = static_cast<int>(p[1]) * 128 / 100; // 0..128
    int rowMax = 0;
    for (int y = 0; y < h; y++) {
        const int g = (grainBase[y] * grainQ8) >> 8;              // +-256
        const int wave = (sl[((y * 3) + ph3) & (SIN_N - 1)] * 64) >> 8; // +-128
        const int v = g + wave;
        rowTerm[y] = static_cast<int16_t>(v);
        const int a = v < 0 ? -v : v;
        if (a > rowMax) {
            rowMax = a;
        }
    }

    // Column term: two broad lobes. p[2] sets how wide they are (a lower
    // spatial frequency is a wider reflection), p[3] how strong.
    const uint32_t fQ4 = 48 - static_cast<uint32_t>(p[2]) * 36 / 100; // 48..12, /16 cycles per px
    const int amp1 = 96 + static_cast<int>(p[3]) * 160 / 100;         // 96..256
    const int amp2 = amp1 >> 1;
    const int mid = (INDEX_MAX + 1) / 2;
    for (int x = 0; x < w; x++) {
        const uint32_t i1 = ((static_cast<uint32_t>(x) * fQ4) >> 4) + ph1;
        const uint32_t i2 = ((static_cast<uint32_t>(x) * fQ4) >> 5) + ph2;
        colTerm[x] = static_cast<int16_t>(mid + ((sl[i1 & (SIN_N - 1)] * amp1) >> 8) +
                                          ((sl[i2 & (SIN_N - 1)] * amp2) >> 8));
    }

    // Expand into the eight y-phase copies, adding the dither and clamping so
    // that colTermPh + rowTerm cannot leave 0..INDEX_MAX for any row.
    const int lo = rowMax;
    const int hi = INDEX_MAX - rowMax;
    for (int ph = 0; ph < 8; ph++) {
        int16_t *dstPh = colTermPh + static_cast<size_t>(ph) * w;
        const int16_t *off = &dithOff[ph * 8];
        for (int x = 0; x < w; x++) {
            int v = colTerm[x] + off[x & 7];
            v = v < lo ? lo : (v > hi ? hi : v);
            dstPh[x] = static_cast<int16_t>(v);
        }
    }
}

// The kernel. Portable C++ on every target, so the descriptor's bandRef slot
// is nullptr: BgAnim.h asks for nullptr when band() is portable code and
// there is no second path to compare it against.
void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int y = y0; y < y0 + rows; y++) {
        const int rt = rowTerm[y];
        const int16_t *ct = colTermPh + static_cast<size_t>(y & 7) * w;
        int x = 0;
        // Pixel pairs as one 32-bit store, which band()'s alignment contract
        // (BgAnim.h) allows: dst is 4-byte aligned and a multi-row call has an
        // even w, so row r at r*w pixels stays aligned. An odd w arrives one
        // row per call and falls through to the tail loop.
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
    releaseTable(colTerm, static_cast<size_t>(allocW) * sizeof(int16_t));
    releaseTable(colTermPh, static_cast<size_t>(8 * allocW) * sizeof(int16_t));
    releaseTable(rowTerm, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(grainBase, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(palette, 256 * sizeof(uint16_t));
    allocW = allocH = 0;
    lastThemeGen = 0xFFFFFFFF;
    lastP[0] = lastP[1] = lastP[2] = lastP[3] = 255;
}

} // namespace

extern const BgAnimation bg_anim_brushed;
const BgAnimation bg_anim_brushed = {
    "brushed",
    "Brushed",
    {{"speed", "Speed", 20}, {"grain", "Grain", 35}, {"reflection", "Reflection", 45}, {"contrast", "Contrast", 30}},
    init,
    frame,
    band,
    release,
    nullptr,
};

#endif // GAGGIMATE_SIM
