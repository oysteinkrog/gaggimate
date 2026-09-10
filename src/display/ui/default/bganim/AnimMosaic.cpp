#ifndef GAGGIMATE_SIM

// "Quiet Mosaic" - large uneven tiles in restrained theme colours, separated
// by a darker grout line, each tile changing brightness on its own slow clock.
// Brainstormed 2026-09-09 with GPT (Codex CLI, gpt-6-astra) as candidate 2 of
// gm-4bd.
//
// The separable kernel again, index = (colTermPh[x] + rowTerm[y]) >> 4, but
// with piecewise constant terms: the column table holds one level per tile
// column and the row table one level per tile row, so a tile's colour is the
// sum of its column's level and its row's level. That is what a tiled screen
// with light behind it looks like, and it is the only shape a two-gather
// separable kernel can make a grid out of.
//
// The obvious failure of a separable grid is that whole rows and columns move
// together and the field reads as sliding bands. Two things stop that here:
// each tile column and each tile row has its own phase, taken from a fixed
// seed at layout time, and its own rate multiplier, so no two neighbours peak
// at the same moment. The tile edges are fixed at layout time as well, so
// nothing about the grid itself crawls.
//
// The grout is the first two pixels of each tile, dropped by a fixed amount in
// both tables. Where a column grout crosses a row grout the two drops add,
// which is what makes the intersections read darker, as real grout does.
//
// Tile widths are a fraction of the render size, so the grid looks the same at
// 480, 240 and 233 wide.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <string.h>

namespace {
using namespace bganim;

constexpr int MAX_TILES = 24;
constexpr int MID = 1200;   // sum units at a tile's rest level, per axis
constexpr int GROUT = 400;  // sum units a grout pixel drops
constexpr int GROUT_PX = 2; // pixels of grout on the leading edge of a tile
constexpr int SHADE = 90;   // sum units a tile's own shading ramps across it

uint8_t *colTile = nullptr; // per column: tile index, bit 7 marks grout. PSRAM
uint8_t *rowTile = nullptr; // per row, same packing. PSRAM
int8_t *colShade = nullptr; // per column: the tile's own shading ramp. PSRAM
int8_t *rowShade = nullptr; // per row, same. PSRAM
int16_t *colTerm = nullptr; // frame() only -> PSRAM
int16_t *colTermPh = nullptr; // read every pixel -> slab, 8 y-phase copies
int16_t *rowTerm = nullptr;   // read once per row -> slab
uint16_t *palette = nullptr;  // read every pixel -> slab

int16_t dithOff[64] = {0};
uint16_t colPhase[MAX_TILES] = {0};
uint16_t rowPhase[MAX_TILES] = {0};
uint8_t colRate[MAX_TILES] = {0};
uint8_t rowRate[MAX_TILES] = {0};
int nColTiles = 0, nRowTiles = 0;

uint8_t lastP[4] = {255, 255, 255, 255};
uint32_t lastThemeGen = 0xFFFFFFFF;
int lastLayoutP1 = -1, lastLayoutP3 = -1;
int allocW = 0, allocH = 0;

// Table placement, against the 9,216 B per-animation slab:
//   colTermPh  7,680 B  read every pixel                   HOT
//   rowTerm      960 B  read once per row                  HOT
//   palette      512 B  read every pixel, data-dependent   HOT
//   -------------------------------------------------------------
//              9,152 B of 9,216 B
//   colTerm      960 B  read only in frame()               PSRAM
//   colTile      480 B  read once per column in frame()    PSRAM
//   rowTile      480 B  read once per row in frame()       PSRAM
//   colShade     480 B  read once per column in frame()    PSRAM
//   rowShade     480 B  read once per row in frame()       PSRAM
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
    if (colTile == nullptr) {
        colTile = static_cast<uint8_t *>(alloc(allocW));
        if (colTile == nullptr) {
            release();
            return false;
        }
    }
    if (colShade == nullptr) {
        colShade = static_cast<int8_t *>(alloc(allocW));
        if (colShade == nullptr) {
            release();
            return false;
        }
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
    if (rowTile == nullptr) {
        rowTile = static_cast<uint8_t *>(alloc(allocH));
        if (rowTile == nullptr) {
            release();
            return false;
        }
    }
    if (rowShade == nullptr) {
        rowShade = static_cast<int8_t *>(alloc(allocH));
        if (rowShade == nullptr) {
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

// One axis of the grid: uneven tile widths from a fixed seed, the leading
// GROUT_PX pixels of each tile marked, and a phase and rate per tile.
int layoutAxis(uint8_t *tile, int8_t *shade, int n, int base, int jitter, uint32_t seed, uint16_t *phase,
               uint8_t *rate) {
    uint32_t s = seed;
    int idx = 0;
    int x = 0;
    while (x < n && idx < MAX_TILES) {
        int wide = base;
        if (jitter > 0) {
            wide += static_cast<int>(nextRand(s) % static_cast<uint32_t>(2 * jitter + 1)) - jitter;
        }
        if (wide < 6) {
            wide = 6;
        }
        // The last tile takes whatever is left rather than starting a tile
        // that would be a sliver against the edge.
        if (n - x - wide < 6 || idx == MAX_TILES - 1) {
            wide = n - x;
        }
        phase[idx] = static_cast<uint16_t>(nextRand(s) & (SIN_N - 1));
        rate[idx] = static_cast<uint8_t>(5 + (nextRand(s) % 8u)); // 5..12, eighths
        // Each tile carries its own shallow shading ramp, with a random sign,
        // so a tile is not a dead flat patch and two neighbours do not shade
        // the same way. This is baked once, at layout time: it is geometry.
        const int sign = (nextRand(s) & 1u) != 0 ? 1 : -1;
        for (int k = 0; k < wide && x < n; k++, x++) {
            tile[x] = static_cast<uint8_t>(idx | (k < GROUT_PX ? 0x80 : 0));
            shade[x] = static_cast<int8_t>((sign * ((2 * k - wide) * SHADE / 2)) / (wide > 0 ? wide : 1));
        }
        idx++;
    }
    // A short axis can leave the tail unassigned if MAX_TILES ran out first.
    for (; x < n; x++) {
        tile[x] = static_cast<uint8_t>(idx > 0 ? idx - 1 : 0);
        shade[x] = 0;
    }
    return idx > 0 ? idx : 1;
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[BG_ANIM_PARAMS]) {
    if (memcmp(p, lastP, 4) != 0 || themeGen() != lastThemeGen) {
        buildThemeRamp(palette, 256);
        lastThemeGen = themeGen();
        const float amp = ditherAmp(palette, 256);
        for (int k = 0; k < 64; k++) {
            const float d = (static_cast<float>(BAYER8[k]) - 31.5f) * (amp * 16.0f / 31.5f);
            dithOff[k] = static_cast<int16_t>(lroundf(d));
        }
    }
    if (p[1] != lastLayoutP1 || p[3] != lastLayoutP3) {
        // Tile size as a fraction of the render size: a twelfth of the panel at
        // the small end, a third at the large end.
        const int baseW = w / 16 + static_cast<int>(p[1]) * (w / 4 - w / 16) / 100;
        const int baseH = h / 16 + static_cast<int>(p[1]) * (h / 4 - h / 16) / 100;
        const int jitW = baseW * static_cast<int>(p[3]) / 250; // up to 40% of the base
        const int jitH = baseH * static_cast<int>(p[3]) / 250;
        nColTiles = layoutAxis(colTile, colShade, w, baseW > 6 ? baseW : 6, jitW, 0xA5C31D7Bu, colPhase, colRate);
        nRowTiles = layoutAxis(rowTile, rowShade, h, baseH > 6 ? baseH : 6, jitH, 0x2F6B49E1u, rowPhase, rowRate);
        lastLayoutP1 = p[1];
        lastLayoutP3 = p[3];
    }
    memcpy(lastP, p, 4);

    // Time. Unsigned, so the millis() wrap is a phase wrap.
    const uint32_t sp = 4 + static_cast<uint32_t>(p[0]) * 44 / 100; // 0.25x..3x
    const uint32_t base = (tMs * sp) >> 11;
    const int16_t *sl = sinLut();
    // Level swing per axis, so a tile's own swing is twice this at most.
    const int ampQ = 150 + static_cast<int>(p[2]) * 350 / 100; // 150..500

    int colLevel[MAX_TILES];
    for (int i = 0; i < nColTiles; i++) {
        const uint32_t idx = ((base * colRate[i]) >> 3) + colPhase[i];
        colLevel[i] = (sl[idx & (SIN_N - 1)] * ampQ) >> 9;
    }
    int rowLevel[MAX_TILES];
    for (int i = 0; i < nRowTiles; i++) {
        const uint32_t idx = ((base * rowRate[i]) >> 3) + rowPhase[i];
        rowLevel[i] = (sl[idx & (SIN_N - 1)] * ampQ) >> 9;
    }

    int rowMax = 0;
    for (int y = 0; y < h; y++) {
        const uint8_t t = rowTile[y];
        int v = MID + rowLevel[t & 0x7F] + rowShade[y] - ((t & 0x80) != 0 ? GROUT : 0);
        if (v < 0) {
            v = 0;
        }
        rowTerm[y] = static_cast<int16_t>(v);
        if (v > rowMax) {
            rowMax = v;
        }
    }
    for (int x = 0; x < w; x++) {
        const uint8_t t = colTile[x];
        int v = MID + colLevel[t & 0x7F] + colShade[x] - ((t & 0x80) != 0 ? GROUT : 0);
        if (v < 0) {
            v = 0;
        }
        colTerm[x] = static_cast<int16_t>(v);
    }

    // The sum has no per-pixel bound check, so the column copies are clamped
    // into what the row term leaves of 0..4095.
    const int hi = 4095 - rowMax;
    for (int ph8 = 0; ph8 < 8; ph8++) {
        int16_t *dstPh = colTermPh + static_cast<size_t>(ph8) * w;
        const int16_t *off = &dithOff[ph8 * 8];
        for (int x = 0; x < w; x++) {
            int v = colTerm[x] + off[x & 7];
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
    releaseTable(colTerm, static_cast<size_t>(allocW) * sizeof(int16_t));
    releaseTable(colTile, static_cast<size_t>(allocW));
    releaseTable(colShade, static_cast<size_t>(allocW));
    releaseTable(colTermPh, static_cast<size_t>(8 * allocW) * sizeof(int16_t));
    releaseTable(rowTerm, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(rowTile, static_cast<size_t>(allocH));
    releaseTable(rowShade, static_cast<size_t>(allocH));
    releaseTable(palette, 256 * sizeof(uint16_t));
    allocW = allocH = 0;
    lastThemeGen = 0xFFFFFFFF;
    lastLayoutP1 = lastLayoutP3 = -1;
    nColTiles = nRowTiles = 0;
    lastP[0] = lastP[1] = lastP[2] = lastP[3] = 255;
}

} // namespace

extern const BgAnimation bg_anim_mosaic;
const BgAnimation bg_anim_mosaic = {
    "mosaic",
    "Mosaic",
    {{"speed", "Speed", 15}, {"size", "Tile size", 45}, {"contrast", "Contrast", 30}, {"variation", "Variation", 55}},
    init,
    frame,
    band,
    release,
    nullptr,
};

#endif // GAGGIMATE_SIM
