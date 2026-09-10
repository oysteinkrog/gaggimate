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
// leaving room for the palette, the unpacked row and the dither. No
// texels, seeds, precision or dither phases are discarded to make the
// tables fit.
//
// Codex (gpt-6-astra) wrote the first draft of init, frame, bandRef and
// release. It ran out of credits before writing a kernel, so the row
// pipeline below, the Xtensa kernel and the QEMU test are this pass.

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

// On by default; the portable reference stays available for a device A/B.
#ifndef GM_BGANIM_CELLS_ASM
#define GM_BGANIM_CELLS_ASM 1
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
// One guard byte would do: the packer's second store for the last texel of
// a row is provably zero, because 96*7 is a whole number of bytes so that
// texel ends inside byte 83. Four keeps the table 4-byte aligned in its own
// right and costs nothing after allocHot's 16-byte rounding.
constexpr int TILE_BYTES = TS * PACK_ROW + 4;
constexpr int PACK_MAX = (1 << PACK_BITS) - 1; // 127, what the mask can yield

uint8_t *baseTile = nullptr;   // B, frame-only source -> PSRAM, 9,216 B
uint8_t *seeds = nullptr;      // fixed coordinate pairs -> PSRAM, 18 B
uint8_t *tile = nullptr;       // packed T -> slab, 8,068 B (8,080 aligned)
uint8_t *rowTex = nullptr;     // one tile row unpacked -> slab, texCap B
uint16_t *palette = nullptr;   // 256-entry theme ramp -> slab, 512 B
int16_t *dither = nullptr;     // page's signed Bayer offsets -> slab, 128 B
const uint16_t **slotTab = nullptr; // eight palette bases -> slab, 32 B

// At 480 wide: 8,080 + 128 + 512 + 128 + 32 = 8,880 B of the 9,216 B slab.
// At 240 wide: 8,816 B. A host build stores 64 B of pointers rather than
// 32 and reaches 8,912 B, still inside the slab. No per-row table is
// needed: absolute y gives both the tile row and the Bayer phase.
// PSRAM owns 9,234 B at either width.
//
// rowTex holds the current tile row with T-50 already unpacked, and it is
// long enough to run past the 96 texel wrap so the pixel loop never tests
// for it: entry i carries texel i % 96. slotTab[k] is the palette entry
// that a pixel with column phase k and packed value 0 selects, so the
// kernel's whole per-pixel job is one add and one gather.
int allocW = 0;
int texCap = 0;
int lastWidth = -1;
int lastDepth = -1;
int rowTexIdx = -1;    // tile row currently unpacked into rowTex, -1 if none
bool kernelSafe = false;
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
    // Enough texels for the widest pixel the row can ask for, plus the
    // slack the wrap-free walk reads past the last group.
    texCap = ((w + 3) >> TPX) + 2;
    // Allocate the hot set first so an OOM in either PSRAM allocation
    // exercises teardown of a partially allocated set, including the slab.
    tile = static_cast<uint8_t *>(allocHot(TILE_BYTES));
    rowTex = static_cast<uint8_t *>(allocHot(static_cast<size_t>(texCap)));
    palette = static_cast<uint16_t *>(allocHot(256 * sizeof(uint16_t)));
    dither = static_cast<int16_t *>(allocHot(64 * sizeof(int16_t)));
    slotTab = static_cast<const uint16_t **>(allocHot(8 * sizeof(const uint16_t *)));
    baseTile = static_cast<uint8_t *>(alloc(TS * TS));
    seeds = static_cast<uint8_t *>(alloc(SEED_N * 2));
    if (tile == nullptr || rowTex == nullptr || palette == nullptr || dither == nullptr ||
        slotTab == nullptr || baseTile == nullptr || seeds == nullptr) {
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
    int lo = 0, hi = 0;
    for (int k = 0; k < 64; k++) {
        // bayerOffsets(..., 1.5, 1) uses lround, ties away from zero.
        // Its endpoints are -2 and +2, not theme-derived ditherAmp().
        const int d = static_cast<int>(lroundf((BAYER8[k] - 31.5f) * (1.5f / 31.5f)));
        dither[k] = static_cast<int16_t>(d);
        if (d < lo) lo = d;
        if (d > hi) hi = d;
    }
    // bandRef() clamps the palette index the way the page does. The kernel
    // folds the dither into a palette base instead, which is only the same
    // arithmetic while no clamp can fire. The packed value is masked to
    // seven bits, so the widest index the kernel can form is
    // IDX_LO + 127 + hi and the narrowest IDX_LO + lo. With this Bayer
    // table that is 48..179, so the clamp is dead and the kernel runs.
    // The check is here rather than assumed because a future dither
    // amplitude could break it silently.
    kernelSafe = (IDX_LO + lo) >= 0 && (IDX_LO + PACK_MAX + hi) <= 255;
    rowTexIdx = -1;
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
    // The tile the unpacked row was taken from no longer exists.
    rowTexIdx = -1;
}

// Unpack one packed tile row into rowTex, then repeat it past the 96 texel
// wrap so the pixel loop can walk straight off the end of the tile row.
// A whole row is 96 * 7 = 672 bits = exactly 84 bytes, so the sliding
// accumulator consumes 84 bytes and never reads the guard. Reading bytes in
// order and shifting them in at rising bit positions is the same bit order
// the packer wrote, low bits of a texel first.
void unpackRow(int tileRow) {
    const uint8_t *src = tile + tileRow * PACK_ROW;
    uint32_t acc = 0;
    int nbits = 0;
    for (int i = 0; i < TS; i++) {
        if (nbits < PACK_BITS) {
            acc |= static_cast<uint32_t>(*src++) << nbits;
            nbits += 8;
        }
        rowTex[i] = static_cast<uint8_t>(acc & PACK_MAX);
        acc >>= PACK_BITS;
        nbits -= PACK_BITS;
    }
    for (int i = TS; i < texCap; i++) {
        rowTex[i] = rowTex[i - TS];
    }
    rowTexIdx = tileRow;
}

// Both rows of a two row band share a tile row, because a texel is four
// pixels tall and a band starts on an even row, so this unpacks once per
// band call in production and once per four rows on the interlaced path.
BGANIM_INLINE void ensureRow(int y) {
    const int tileRow = (y >> TPX) % TS;
    if (tileRow != rowTexIdx) {
        unpackRow(tileRow);
    }
}

// The page's pixel loop. The texel is read once per four pixels rather than
// once per pixel, which is the only change: the index, the dither add, the
// clamp and the gather are the page's own. Absolute y determines both
// phases, even for rows==1 or parity-skipping calls, and rowTex is a memo
// of the tile row that y selects, so a single interlaced row is identical
// to that row in a full frame call. The same four-pixel footprint applies
// at 240.
GM_ANIM_IRAM void bandRef(uint16_t *__restrict dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int row = 0; row < rows; row++) {
        const int y = y0 + row;
        ensureRow(y);
        const int16_t *dr = dither + (y & 7) * 8;
        uint16_t *out = dst + static_cast<size_t>(row) * w;
        int x = 0;
        int t = 0;
        while (x < w) {
            const int v = IDX_LO + rowTex[t++];
            const int stop = (w - x) < (1 << TPX) ? w : x + (1 << TPX);
            for (; x < stop; x++) {
                int i = v + dr[x & 7];
                if (i < 0) i = 0;
                else if (i > 255) i = 255;
                out[x] = palette[i];
            }
        }
    }
}

#if GM_BGANIM_CELLS_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
// GCC 14.2's compiled bandRef was read from xtensa-asm14/AnimCells.S both
// before and after the row pipeline above, because the second reading is
// the honest baseline. The draft's loop was 26 instructions per pixel: a
// column-table load, tx*7 by SUBX8, SRAI and EXTUI for the byte and bit
// position, two L8UI, SLLI and OR to join them, SSR and SRL to align the
// field, EXTUI to mask it, ADDI for IDX_LO, an L16SI for the dither, an
// ADD, MIN, MAX, EXTUI, ADDX2 and L16UI for the gather, and the store.
// unpackRow() pays that bit work 96 times per tile row instead of 480
// times per screen row, and the column table is gone because the walk is
// regular, so the current bandRef compiles to 12 instructions per pixel in
// its innermost loop plus about 10 per four-pixel group outside it, near
// 14.5 per pixel in all.
//
// What is left per pixel is a gather, and PIE has no gather, so this kernel
// is scalar by design rather than by omission. It is worth stating what was
// rejected. A vector dither add would need the eight indices back as
// scalars, so it costs a store and eight loads to save eight adds that are
// already folded into slotTab. A vector store of eight finished colours
// would need four EE.MOVI.32.Q plus four packs, more than the eight S16I it
// replaces. A per-row table mapping a packed value straight to a colour
// pair would cut the loop to two instructions per pixel, but it is 12,928 B
// over the eight Bayer phases and so lives in PSRAM, which CLAUDE.md's
// table-placement rule says can cost more than the instructions it saves.
// That one is worth a device A/B and is not guessed at here.
//
// The edge over the compiler is the shape of the loop, not clever
// arithmetic. Four consecutive pixels share a texel and eight consecutive
// pixels share the whole Bayer row, so one group of eight pixels reads two
// texel bytes and eight palette bases, and every pixel is then one ADDX2,
// one L16UI and one S16I. Four independent chains run interleaved, and each
// gather loads its colour back into the register that held its address, so
// every consumer sits at least two instructions after its producer: no
// load-use interlock anywhere in the body, and no taken branch because the
// trip count drives a hardware loop. 36 instructions per eight pixels, 4.5
// per pixel against the compiler's 14.5 for the same work and 26 for the
// draft's.
//
// No coprocessor instruction appears here, so there is no CPENABLE concern
// and no q register state: FreeRTOS's lazy CP3 enable is untouched. Every
// access is a scalar load or store, so nothing depends on 16-byte
// alignment; out is uint16_t* and so 2-byte aligned for S16I, and slotTab
// is a table of pointers and so 4-byte aligned for L32I. The body is 36
// instructions of at most three bytes each, well inside LOOP's 256 byte
// limit, and there is no nesting. IRAM keeps the loop out of flash, which
// CLAUDE.md's render-loop finding says matters more than its instruction
// count when the other core is drawing.
//
// Contract: out[x] = slot[x & 7][tex[x >> 2]] for x in [0, w). The caller
// guarantees tex is readable through index (w - 1) >> 2 and that no palette
// index the pair can form leaves 0..255, which init() checks once.
GM_ANIM_IRAM __attribute__((noinline)) void cellsRowAsm(uint16_t *out, const uint8_t *tex,
                                                        const uint16_t *const *slot, int w) {
    if (w <= 0) {
        return;
    }
    uint16_t *o = out;
    const uint8_t *t = tex;
    int n = w >> 3;
    int v0, v1, pa, pb, pc, pd;
    asm volatile("loopnez %[n], 1f\n"
                 "l8ui    %[v0], %[t], 0\n"
                 "l8ui    %[v1], %[t], 1\n"
                 "l32i    %[pa], %[s], 0\n"
                 "l32i    %[pb], %[s], 4\n"
                 "addx2   %[pa], %[v0], %[pa]\n"
                 "addx2   %[pb], %[v0], %[pb]\n"
                 "l32i    %[pc], %[s], 8\n"
                 "l32i    %[pd], %[s], 12\n"
                 "l16ui   %[pa], %[pa], 0\n"
                 "l16ui   %[pb], %[pb], 0\n"
                 "addx2   %[pc], %[v0], %[pc]\n"
                 "addx2   %[pd], %[v0], %[pd]\n"
                 "s16i    %[pa], %[o], 0\n"
                 "s16i    %[pb], %[o], 2\n"
                 "l16ui   %[pc], %[pc], 0\n"
                 "l16ui   %[pd], %[pd], 0\n"
                 "l32i    %[pa], %[s], 16\n"
                 "l32i    %[pb], %[s], 20\n"
                 "s16i    %[pc], %[o], 4\n"
                 "s16i    %[pd], %[o], 6\n"
                 "addx2   %[pa], %[v1], %[pa]\n"
                 "addx2   %[pb], %[v1], %[pb]\n"
                 "l32i    %[pc], %[s], 24\n"
                 "l32i    %[pd], %[s], 28\n"
                 "l16ui   %[pa], %[pa], 0\n"
                 "l16ui   %[pb], %[pb], 0\n"
                 "addx2   %[pc], %[v1], %[pc]\n"
                 "addx2   %[pd], %[v1], %[pd]\n"
                 "s16i    %[pa], %[o], 8\n"
                 "s16i    %[pb], %[o], 10\n"
                 "l16ui   %[pc], %[pc], 0\n"
                 "l16ui   %[pd], %[pd], 0\n"
                 "addi    %[t], %[t], 2\n"
                 "s16i    %[pc], %[o], 12\n"
                 "s16i    %[pd], %[o], 14\n"
                 "addi    %[o], %[o], 16\n"
                 "1:\n"
                 : [o] "+&r"(o), [t] "+&r"(t), [v0] "=&r"(v0), [v1] "=&r"(v1), [pa] "=&r"(pa),
                   [pb] "=&r"(pb), [pc] "=&r"(pc), [pd] "=&r"(pd)
                 : [s] "r"(slot), [n] "r"(n)
                 : "memory");
    // The groups covered w & ~7 pixels. Finish the row the same way, which
    // production never reaches at 480 or 240 but a 466 or 233 wide caller
    // would.
    for (int x = w & ~7; x < w; x++) {
        out[x] = slot[x & 7][tex[x >> TPX]];
    }
}

void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
    if (!kernelSafe) {
        bandRef(dst, y0, rows, w, tMs, p);
        return;
    }
    for (int row = 0; row < rows; row++) {
        const int y = y0 + row;
        ensureRow(y);
        const int16_t *dr = dither + (y & 7) * 8;
        for (int k = 0; k < 8; k++) {
            slotTab[k] = palette + IDX_LO + dr[k];
        }
        cellsRowAsm(dst + static_cast<size_t>(row) * w, rowTex, slotTab, w);
    }
}
#else
void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
    bandRef(dst, y0, rows, w, tMs, p);
}
#endif

void release() {
    releaseTable(seeds, SEED_N * 2);
    releaseTable(baseTile, TS * TS);
    releaseTable(slotTab, 8 * sizeof(const uint16_t *));
    releaseTable(dither, 64 * sizeof(int16_t));
    releaseTable(palette, 256 * sizeof(uint16_t));
    releaseTable(rowTex, static_cast<size_t>(texCap));
    releaseTable(tile, TILE_BYTES);
    allocW = 0;
    texCap = 0;
    rowTexIdx = -1;
    kernelSafe = false;
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
