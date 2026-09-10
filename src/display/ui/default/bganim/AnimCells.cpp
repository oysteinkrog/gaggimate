#ifndef GAGGIMATE_SIM

// "Cells": broad, softly lit Voronoi channels drifting right and upward.
// This is entry 41 of tools/animbench/web/anim_bench.html: nine toroidal
// seeds, a raised-cosine channel and a wider halo over shaded interiors.
// B is the stationary 96x96 byte tile. frame() translates it into T with
// the page's Q8 bilinear arithmetic, including its two rounding stages.
// A texel covers 4x4 screen pixels, with independent 8x8 Bayer dither at
// every screen pixel. The theme ramp supplies the final RGB565 colours.
//
// The page's header still says 64 seconds, but its updated TS=96 and
// velocities (1,-3) texels/s repeat in 96 seconds at Speed=50. Follow the
// rendered code: 4 px/s right and 12 px/s up, a 384 pixel spatial repeat.
//
// T alone would fill the animation's 9,216 B slab. Its exact range is
// 50..150, so storing T-50 in seven bits preserves every sample while
// leaving room for the palette, column map and dither. No texels, seeds,
// precision or dither phases are discarded to make the tables fit.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>
#include <string.h>

#if defined(ESP_PLATFORM)
#include <esp_attr.h>
#define GM_ANIM_IRAM IRAM_ATTR
#else
#define GM_ANIM_IRAM
#endif

namespace {
using namespace bganim;

constexpr int TS = 96;
constexpr int TPX = 2;          // 1 << TPX = four screen pixels per texel
constexpr int CH_W = 9;         // channel half-width, before the Width knob
constexpr int IDX_LO = 50;
constexpr int IDX_AMP = 70;
constexpr int HALO_AMP = 30;
constexpr float HALO_MUL = 3.2f;
constexpr int SEED_N = 9;
constexpr int PACK_BITS = 7;    // T-50 is 0..100, exactly representable
constexpr int PACK_ROW = TS * PACK_BITS / 8; // 84 B, also word aligned
// Four guard bytes let the kernel read two aligned words even at the
// final packed pair. allocHot rounds 8,068 B to 8,080 B, aligning all
// subsequent tables to 16 bytes as well.
constexpr int TILE_BYTES = TS * PACK_ROW + 4;

uint8_t *baseTile = nullptr;   // B, frame-only source -> PSRAM, 9,216 B
uint8_t *seeds = nullptr;      // fixed coordinate pairs -> PSRAM, 18 B
uint8_t *tile = nullptr;       // packed T -> slab, 8,068 B (8,080 aligned)
uint8_t *xt = nullptr;         // page's column-to-texel map -> slab, w B
uint16_t *palette = nullptr;   // 256-entry theme ramp -> slab, 512 B
int16_t *dither = nullptr;     // page's signed Bayer offsets -> slab, 128 B

// At 480 wide: 8,080 + 480 + 512 + 128 = 9,200 B of the 9,216 B slab.
// At 240 wide: 8,960 B. No per-row table is needed: absolute y gives
// both the tile row and Bayer phase. PSRAM owns 9,234 B at either width.
int allocW = 0;
int lastWidth = -1;
int lastDepth = -1;
uint32_t lastThemeGen = 0xFFFFFFFF;

void release();

bool init(int w, int) {
    // Idempotent at a fixed size; also safe if a caller resizes directly.
    if (allocW != 0 && allocW != w) {
        release();
    }
    if (tile != nullptr) {
        return true;
    }
    allocW = w;
    // Allocate the hot set first so an OOM in either PSRAM allocation
    // exercises teardown of a partially allocated set, including the slab.
    tile = static_cast<uint8_t *>(allocHot(TILE_BYTES));
    xt = static_cast<uint8_t *>(allocHot(static_cast<size_t>(w)));
    palette = static_cast<uint16_t *>(allocHot(256 * sizeof(uint16_t)));
    dither = static_cast<int16_t *>(allocHot(64 * sizeof(int16_t)));
    baseTile = static_cast<uint8_t *>(alloc(TS * TS));
    seeds = static_cast<uint8_t *>(alloc(SEED_N * 2));
    if (tile == nullptr || xt == nullptr || palette == nullptr || dither == nullptr ||
        baseTile == nullptr || seeds == nullptr) {
        release();
        return false;
    }
    // The page's nine seeds, in its original order. These constants are
    // written into the allocated table, never kept in an internal BSS LUT.
    seeds[0] = 12; seeds[1] = 10;
    seeds[2] = 45; seeds[3] = 20;
    seeds[4] = 78; seeds[5] = 8;
    seeds[6] = 20; seeds[7] = 44;
    seeds[8] = 52; seeds[9] = 50;
    seeds[10] = 84; seeds[11] = 40;
    seeds[12] = 8; seeds[13] = 76;
    seeds[14] = 38; seeds[15] = 84;
    seeds[16] = 70; seeds[17] = 72;
    for (int x = 0; x < w; x++) {
        xt[x] = static_cast<uint8_t>((x >> TPX) % TS);
    }
    for (int k = 0; k < 64; k++) {
        // bayerOffsets(..., 1.5, 1) uses lround, ties away from zero.
        // Its endpoints are -2 and +2, not theme-derived ditherAmp().
        dither[k] = static_cast<int16_t>(lroundf((BAYER8[k] - 31.5f) * (1.5f / 31.5f)));
    }
    return true;
}

void buildBase(uint8_t width) {
    const float chw = CH_W * (0.6f + width * 0.9f / 100.0f);
    const float invChw = 1.0f / chw;
    const float invHalo = 1.0f / (chw * HALO_MUL);
    constexpr float PI = 3.14159265358979323846f;
    for (int ty = 0; ty < TS; ty++) {
        for (int tx = 0; tx < TS; tx++) {
            float d1 = 1.0e9f, d2 = 1.0e9f;
            for (int s = 0; s < SEED_N; s++) {
                int dx = abs(tx - seeds[s * 2]);
                int dy = abs(ty - seeds[s * 2 + 1]);
                if (dx > TS / 2) dx = TS - dx;
                if (dy > TS / 2) dy = TS - dy;
                const float d = sqrtf(static_cast<float>(dx * dx + dy * dy));
                if (d < d1) {
                    d2 = d1;
                    d1 = d;
                } else if (d < d2) {
                    d2 = d;
                }
            }
            float z = (d2 - d1) * invChw;
            float zh = (d2 - d1) * invHalo;
            if (z > 1.0f) z = 1.0f;
            if (zh > 1.0f) zh = 1.0f;
            // Round each raised cosine separately, exactly as the page.
            // sqrtf/cosf are only used when Width changes, never in band().
            baseTile[ty * TS + tx] = static_cast<uint8_t>(IDX_LO +
                lroundf(IDX_AMP * (1.0f + cosf(PI * z)) * 0.5f) +
                lroundf(HALO_AMP * (1.0f + cosf(PI * zh)) * 0.5f));
        }
    }
}

void frame(uint32_t tMs, int, int, const uint8_t p[4]) {
    if (p[1] != lastWidth) {
        buildBase(p[1]);
        lastWidth = p[1];
    }
    const uint32_t gen = themeGen();
    if (p[2] != lastDepth || gen != lastThemeGen) {
        // This is the page's Contrast control, including its brightness
        // scale: 176 + round(depth*80/100), 224 at the default 60.
        buildThemeRamp(palette, static_cast<uint16_t>(176 + (p[2] * 80 + 50) / 100));
        lastDepth = p[2];
        lastThemeGen = gen;
    }

    // The page rounds positive 256*seconds*velocity before wrapping.
    // Represent speedMul's float exactly as Q27, then multiply tMs in
    // uint64 so long uptime does not erase sub-texel motion in float(tMs).
    // 1000*2^(27-8) converts that product to Q8 texels. Even Speed=100,
    // tMs=UINT32_MAX and velocity=3 keep the numerator below 2^64.
    // The only difference from JS double is speedMul's normal float
    // rounding (and sqrtf/cosf at a profile rounding boundary).
    const uint32_t speedQ27 = static_cast<uint32_t>(speedMul(p[0]) * 134217728.0f);
    const uint64_t clockQ27 = static_cast<uint64_t>(tMs) * speedQ27;
    constexpr uint64_t Q8_DIV = 1000u * (1u << 19);
    constexpr int WRAP_Q8 = TS * 256;
    const int xq = static_cast<int>(((clockQ27 + Q8_DIV / 2) / Q8_DIV) % WRAP_Q8);
    const int oyq = static_cast<int>(((clockQ27 * 3u + Q8_DIV / 2) / Q8_DIV) % WRAP_Q8);
    const int oxq = xq == 0 ? 0 : WRAP_Q8 - xq; // sample left to move right
    const int ix = oxq >> 8, fx = oxq & 255;
    const int iy = oyq >> 8, fy = oyq & 255;

    memset(tile, 0, TILE_BYTES); // also initializes the aligned-word guard
    for (int ty = 0; ty < TS; ty++) {
        const int sy0 = (ty + iy) % TS;
        const int sy1 = sy0 + 1 == TS ? 0 : sy0 + 1;
        const uint8_t *r0 = baseTile + sy0 * TS;
        const uint8_t *r1 = baseTile + sy1 * TS;
        uint8_t *out = tile + ty * PACK_ROW;
        for (int tx = 0; tx < TS; tx++) {
            const int sx0 = (tx + ix) % TS;
            const int sx1 = sx0 + 1 == TS ? 0 : sx0 + 1;
            const int a = (r0[sx0] << 8) + (r0[sx1] - r0[sx0]) * fx;
            const int b = (r1[sx0] << 8) + (r1[sx1] - r1[sx0]) * fx;
            // Signed >>8 floors a negative vertical delta on both host
            // GCC and Xtensa, matching JS >>. Keep the +128 AFTER that
            // shift: merging the roundings can change a tile value by one.
            const int v = ((a + (((b - a) * fy) >> 8) + 128) >> 8) - IDX_LO;
            const int bit = tx * PACK_BITS;
            const unsigned packed = static_cast<unsigned>(v) << (bit & 7);
            out[bit >> 3] |= static_cast<uint8_t>(packed);
            out[(bit >> 3) + 1] |= static_cast<uint8_t>(packed >> 8);
        }
    }
}

BGANIM_INLINE int texel(const uint8_t *row, int tx) {
    const int bit = tx * PACK_BITS;
    const uint8_t *p = row + (bit >> 3);
    return IDX_LO + (((p[0] | (static_cast<unsigned>(p[1]) << 8)) >> (bit & 7)) & 127);
}

// The page's pixel loop, with only a lossless unpack before its dither
// add and clamp. There is no hand written Xtensa kernel yet, so band()
// runs this on every target, including the device. Absolute y determines both phases, even for rows==1 or
// parity-skipping calls. The same four-pixel footprint applies at 240.
GM_ANIM_IRAM void bandRef(uint16_t *__restrict dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int row = 0; row < rows; row++) {
        const int y = y0 + row;
        const uint8_t *tr = tile + ((y >> TPX) % TS) * PACK_ROW;
        const int16_t *dr = dither + (y & 7) * 8;
        uint16_t *out = dst + static_cast<size_t>(row) * w;
        for (int x = 0; x < w; x++) {
            int i = texel(tr, xt[x]) + dr[x & 7];
            if (i < 0) i = 0;
            else if (i > 255) i = 255;
            out[x] = palette[i];
        }
    }
}

void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
    bandRef(dst, y0, rows, w, tMs, p);
}

void release() {
    releaseTable(seeds, SEED_N * 2);
    releaseTable(baseTile, TS * TS);
    releaseTable(dither, 64 * sizeof(int16_t));
    releaseTable(palette, 256 * sizeof(uint16_t));
    releaseTable(xt, static_cast<size_t>(allocW));
    releaseTable(tile, TILE_BYTES);
    allocW = 0;
    lastWidth = lastDepth = -1;
    lastThemeGen = 0xFFFFFFFF;
}

} // namespace

extern const BgAnimation bg_anim_cells;
const BgAnimation bg_anim_cells = {
    "cells",
    "Cells",
    {{"speed", "Speed", 50},
     {"width", "Channel width", 55},
     {"depth", "Contrast", 60},
     {nullptr, nullptr, 0}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
