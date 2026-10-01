#ifndef GAGGIMATE_SIM

// "Mosaic": large, uneven tiles that brighten independently under a slow
// diagonal wash. This is entry 18 in tools/animbench/web/anim_bench.html:
// cubed raised sines light a few tiles at a time, 14-pixel smoothstep seams
// join their levels, and signed sine domes shade the tile interiors.
//
// Field values have four fractional palette-index bits. Tile levels retain
// the page's Q9 cube, seam weights are Q8, and the final signed sum is shifted
// by four before clamping to the theme palette. All rounding shifts, PRNG
// draws and unsigned 32-bit time wraps follow the page. No float runs per
// pixel. The shared sine table can differ from JavaScript's double sine by a
// rounding unit; the host PPM also expands RGB565 with division rather than
// the page's bit replication. Neither changes the field or its motion.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <string.h>

#ifndef GM_BGANIM_MOSAIC_ASM
#define GM_BGANIM_MOSAIC_ASM 1
#endif

namespace {
using namespace bganim;

constexpr int MAX_TILES = 20;           // page's limit per axis, also bounds the tile RNG tables
constexpr int MID = 560;                // resting field at Brightness 50, in sixteenths of a palette index
constexpr int BEVEL = 190;              // signed tile-interior dome amplitude at Bevel 50, same units
constexpr int WASH = 210;               // diagonal wash amplitude at Wash 50, same units
constexpr int BLEND_PX = 7;             // seven pixels on each side of a seam, fourteen total
constexpr int MIN_WIDTH = 3 * BLEND_PX; // keeps a tile's two seam regions disjoint
constexpr int TILE_COUNT = MAX_TILES * MAX_TILES;
static_assert(SIN_N == 1024 && SIN_AMP == 512 && BLEND_PX == 7, "Mosaic's Q9 math and seam codes mirror the page");

// Only frame() reads the axis geometry and tile clocks. The combined column
// tables and compressed row records are the tables band() actually reads.
int16_t *colShade = nullptr, *rowShade = nullptr, *washCol = nullptr;
uint8_t *cA = nullptr, *cB = nullptr;
uint16_t *cW = nullptr;
int16_t *level = nullptr;
uint16_t *tPhase = nullptr;
uint8_t *tRate = nullptr;
uint16_t *ramp = nullptr;

int16_t *comb = nullptr;        // one aligned column table per reachable tile row
int16_t *rowTerm = nullptr;     // page's rowShade[y] + washRow[y]
uint8_t *rowBlend = nullptr;    // high nibble: tile A; low nibble: seam position
uint16_t *seamWeight = nullptr; // sixteen Q8 weights; code 0 means no interpolation
int16_t *dith = nullptr;        // 8x8 Bayer offsets, four fractional index bits
uint16_t *palette = nullptr;    // the page's nonlinear remap of the theme ramp
const int16_t *sl = nullptr;    // borrowed shared 1024-entry sine, never released here

int allocW = 0, allocH = 0, combStride = 0, combRows = 0;
int nCol = 1, nRow = 1;
int lastSize = -1, lastVariation = -1, lastBevel = -1;
uint32_t lastThemeGen = 0;
bool paletteValid = false;

// At 480x480, the fixed seeds and every size/variation combination require
// at most seven tile rows. init() computes that bound, rather than trusting a
// hard-coded observation or reserving all twenty rows of the page's array.
// Stride rounds up to eight int16 elements so every PIE source row is aligned.
// Slab: comb 6,736 (includes one 16-byte lookahead guard), rowTerm 960,
// rowBlend 480, seamWeight 32, dith 128, palette 512 = 8,848 B.
// No per-pixel or per-row table lives in PSRAM.
// PSRAM: colShade/rowShade/washCol/cW 960 each, cA/cB 480 each,
// level/tPhase 800 each, tRate 400, ramp 512 = 7,312 B.
// Temporary layout arrays and the kernel's eight indices live on the stack.
// No row caching across band calls: interlace and repeated-row timing see
// the same work and the same pixels for a given absolute y.
void release();

// allocHot charges whole 16-byte blocks, including at odd render widths.
size_t roundHot(size_t n) { return (n + 15u) & ~static_cast<size_t>(15u); }

// Layout and capacity calculation share the exact same PRNG walk. Even the
// count-only call consumes the sign draw, which changes subsequent widths.
int axisWidths(int *start, int *width, int8_t *signs, int n, int wide0, int jitter, uint32_t seed) {
    uint32_t s = seed;
    int x = 0, count = 0;
    while (x < n && count < MAX_TILES) {
        int wide = wide0;
        if (jitter > 0) {
            wide += static_cast<int>(nextRand(s) % static_cast<uint32_t>(2 * jitter + 1)) - jitter;
        }
        if (wide < MIN_WIDTH)
            wide = MIN_WIDTH;
        if (n - x - wide < MIN_WIDTH || count == MAX_TILES - 1)
            wide = n - x;
        const int sign = (nextRand(s) & 1u) ? 1 : -1;
        if (start != nullptr) {
            start[count] = x;
            width[count] = wide;
            signs[count] = static_cast<int8_t>(sign);
        }
        x += wide;
        ++count;
    }
    return count > 0 ? count : 1;
}

// Size moves the nominal tile width from one sixth to two fifths of an
// axis. Variation's later /250 adds up to +/-40 percent of that width.
int baseWidth(int n, int size) { return n / 6 + size * (n * 2 / 5 - n / 6) / 100; }

bool init(int w, int h) {
    if (comb != nullptr && allocW == w && allocH == h)
        return true;
    release();
    if (w <= 0 || h <= 0)
        return false;
    sl = sinLut();
    if (sl == nullptr) {
        release();
        return false;
    }
    allocW = w;
    allocH = h;
    combStride = (w + 7) & ~7;
    // Exhaust the finite slider domain once per init, outside frame/band.
    // This preserves every layout the page can select while removing unused
    // rows from its MAX_TILES*w allocation. No allocation when sliders move.
    for (int size = 0; size <= 100; ++size) {
        const int base = baseWidth(h, size);
        for (int variation = 0; variation <= 100; ++variation) {
            const int count = axisWidths(nullptr, nullptr, nullptr, h, base, base * variation / 250, 0x2F6B49E1u);
            if (count > combRows)
                combRows = count;
        }
    }
    const size_t hotBytes = roundHot((static_cast<size_t>(combRows) * combStride + 8) * 2) +
                            roundHot(static_cast<size_t>(h) * 2) + roundHot(h) + 32 + 128 + 512;
    // Four bits encode tile A; reject unsupported dimensions before asking
    // allocHot to spill. All 480/466/240/233 panel modes fit with room left.
    if (combRows > 16 || hotBytes > HOT_SLAB_BYTES - HOT_SHARED_RESERVE) {
        release();
        return false;
    }
    comb = static_cast<int16_t *>(allocHot((static_cast<size_t>(combRows) * combStride + 8) * sizeof(int16_t)));
    rowTerm = static_cast<int16_t *>(allocHot(h * sizeof(int16_t)));
    rowBlend = static_cast<uint8_t *>(allocHot(h));
    seamWeight = static_cast<uint16_t *>(allocHot(16 * sizeof(uint16_t)));
    dith = static_cast<int16_t *>(allocHot(64 * sizeof(int16_t)));
    palette = static_cast<uint16_t *>(allocHot(256 * sizeof(uint16_t)));
    colShade = static_cast<int16_t *>(alloc(w * sizeof(int16_t)));
    rowShade = static_cast<int16_t *>(alloc(h * sizeof(int16_t)));
    washCol = static_cast<int16_t *>(alloc(w * sizeof(int16_t)));
    cA = static_cast<uint8_t *>(alloc(w));
    cB = static_cast<uint8_t *>(alloc(w));
    cW = static_cast<uint16_t *>(alloc(w * sizeof(uint16_t)));
    level = static_cast<int16_t *>(alloc(TILE_COUNT * sizeof(int16_t)));
    tPhase = static_cast<uint16_t *>(alloc(TILE_COUNT * sizeof(uint16_t)));
    tRate = static_cast<uint8_t *>(alloc(TILE_COUNT));
    ramp = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
    if (!comb || !rowTerm || !rowBlend || !seamWeight || !dith || !palette || !colShade || !rowShade || !washCol || !cA || !cB ||
        !cW || !level || !tPhase || !tRate || !ramp) {
        release(); // releases the entire partial set before a retry (gm-bzu.15)
        return false;
    }
    // allocHot aligns these in the slab. Also guard its PSRAM fallback,
    // whose allocator promises only four bytes if another table owns the
    // slab: no successful init may hand PIE a misaligned address.
    if ((reinterpret_cast<uintptr_t>(comb) | reinterpret_cast<uintptr_t>(dith)) & 15u) {
        release();
        return false;
    }
    // The pipelined kernel reads one extra vector, never uses or writes it.
    // Pad the last table as well as each odd-width row, and initialise the
    // unused rows so every such load reads defined storage.
    memset(comb, 0, (static_cast<size_t>(combRows) * combStride + 8) * sizeof(int16_t));
    seamWeight[0] = seamWeight[15] = 0; // interior; code 15 is unused
    for (int code = 1; code <= 14; ++code) {
        // Codes 1..7 are the left half of a seam, 8..14 the right half.
        // 128 is its midpoint in Q8; 64 sets the half-pixel sample spacing.
        const int u = code <= 7 ? 128 - ((15 - 2 * code) * 64 / BLEND_PX) : 128 + ((2 * (code - 8) + 1) * 64 / BLEND_PX);
        // 768 = 3*256: u^2*(3-2u) with u in Q8, returned in Q8.
        seamWeight[code] = static_cast<uint16_t>((u * u * (768 - 2 * u)) >> 16);
    }
    return true;
}

// The column tables retain the page's A/B/weight representation. Rows use
// one byte instead: tile B is always A+1 at a seam, and the fourteen possible
// smoothstep weights are selected by a nibble. This is lossless compression.
int layoutAxis(int16_t *shade, uint8_t *a, uint8_t *b, uint16_t *weight, uint8_t *blend, int n, int base, int jitter,
               uint32_t seed, int bevel) {
    int start[MAX_TILES], width[MAX_TILES];
    int8_t signs[MAX_TILES];
    const int count = axisWidths(start, width, signs, n, base, jitter, seed);
    for (int i = 0; i < count; ++i) {
        const int wide = width[i];
        for (int k = 0; k < wide; ++k) {
            const int x = start[i] + k;
            const int u = k * (SIN_N / 2) / wide; // half sine across the tile
            shade[x] = static_cast<int16_t>((signs[i] * sl[u & (SIN_N - 1)] * bevel) >> 9);
            int ta = i, tb = i, code = 0;
            const int e = wide - 1 - k;
            if (k < BLEND_PX && i > 0) {
                ta = i - 1;
                code = 8 + k;
            } else if (e < BLEND_PX && i < count - 1) {
                tb = i + 1;
                code = 7 - e;
            }
            if (blend != nullptr) {
                blend[x] = static_cast<uint8_t>((ta << 4) | code);
            } else {
                a[x] = static_cast<uint8_t>(ta);
                b[x] = static_cast<uint8_t>(tb);
                weight[x] = seamWeight[code];
            }
        }
    }
    return count;
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[BG_ANIM_PARAMS]) {
    const uint32_t gen = themeGen();
    if (!paletteValid || gen != lastThemeGen) {
        buildThemeRamp(ramp, 256);
        // The page lifts black to theme index 5 and combines linear weight
        // 95 with quadratic weight 105. The resulting ramp spans 5..203,
        // keeping the brightest tiles below the theme's brightest stop.
        for (int i = 0; i < 256; ++i) {
            const int i2 = (i * i) >> 8;
            palette[i] = ramp[5 + ((i * 95) >> 8) + ((i2 * 105) >> 8)];
        }
        // Dither follows the remapped palette, not the raw theme ramp. The
        // page's bayerOffsets(...,16) keeps sub-index offsets until band().
        // 31.5 is the midpoint of Bayer8's 0..63 entries.
        const float amp = ditherAmp(palette, 256);
        for (int k = 0; k < 64; ++k) {
            dith[k] = static_cast<int16_t>(lroundf((static_cast<float>(BAYER8[k]) - 31.5f) * (amp * 16.0f / 31.5f)));
        }
        lastThemeGen = gen;
        paletteValid = true;
    }
    if (p[1] != lastSize || p[3] != lastVariation || p[4] != lastBevel) {
        const int bw = baseWidth(w, p[1]), bh = baseWidth(h, p[1]);
        // Bevel (p[4]) scales the tile domes: flat tiles at 0, 190 at 50,
        // 380 at 100. It changes no tile boundary or tile clock.
        const int bevel = BEVEL * p[4] / 50;
        nCol = layoutAxis(colShade, cA, cB, cW, nullptr, w, bw, bw * p[3] / 250, 0xA5C31D7Bu, bevel);
        nRow = layoutAxis(rowShade, nullptr, nullptr, nullptr, rowBlend, h, bh, bh * p[3] / 250, 0x2F6B49E1u, bevel);
        uint32_t s = 0x6C8E9CF7u;
        for (int i = 0; i < nRow * nCol; ++i) {
            tPhase[i] = static_cast<uint16_t>(nextRand(s) & (SIN_N - 1));
            tRate[i] = static_cast<uint8_t>(4 + nextRand(s) % 8u); // four through eleven
        }
        lastSize = p[1];
        lastVariation = p[3];
        lastBevel = p[4];
    }
    // Wrapping each multiplication BEFORE shifting matches JavaScript's
    // >>>0 exactly. That is what the old integer ramp was for: it kept
    // tMs * sp an exact uint32 product with no rounding convention to agree
    // on, and it kept the phase clocks on the full 32-bit time base. A Q8
    // multiplier keeps both properties. The product stays exact because
    // 2^32 * 27502, the largest speedQ8 can make, is 1.2e14 and the page's
    // double holds integers to 9.0e15, and the shifts below are chosen so a
    // uint32 wrap of any of these products moves the sine index by a whole
    // number of 1024-entry cycles: the largest shift is 18 and 2^32 >> 18
    // is 16 cycles.
    // The integer property the ramp existed for therefore survives; only
    // the rate it maps p[0] to changes.
    //
    // Speed follows the fleet's curve, speedMul() (bead gm-kh2s): 0.15x at
    // 0, 1x at 50 and 6.7x at 100 of the rate the calibration set. The
    // ramps before it were 4 + p*44/100 (sp 4 to 48) and then, from the
    // calibration (gm-33fm), 2 + p*28/100 (sp 2 to 30, 16 at Speed 50).
    // The first covered 12x of rate end to end and the second 15x, where
    // the fleet covers 45x, so the top half of the slider was nearly flat. The multiplier is Q8 and 4096
    // at 50, the calibration's 16 with eight more fraction bits, so the
    // shifts below are the old 9, 10 and 10 plus eight and every clock at
    // Speed 50 is unchanged. Rounded, not truncated: the nearest .5
    // boundary over Speed 0 to 100 is 38 float ulps away, so exp2f on the
    // host, exp2f on the device and Math.pow on the page land on the same
    // integer.
    //
    // This animation used to ship a Speed default of 15, so the shipped
    // picture ran at sp 10 and nobody saw the setting the fleet sweep
    // measured. The default is 50 now, the same as every other animation, so
    // the calibrated rate is the one a user gets without touching the slider.
    const uint32_t speedQ8 = static_cast<uint32_t>(lroundf(4096.0f * speedMul(p[0])));
    const uint32_t base = tMs * speedQ8;
    const uint32_t phW1 = base >> 17, phW2 = (base * 3u) >> 18;
    // Contrast sets a tile's maximum excursion to 1900..3600 Q4 units.
    const int amp = 1900 + static_cast<int>(p[2]) * 1700 / 100;
    // A tile's period is 268,435,456/(speedQ8*rate) ms, 5.96..16.38 s at
    // the default Speed 50. The page header rounds that to 6..16 s.
    for (int i = 0; i < nRow * nCol; ++i) {
        const uint32_t idx = ((base * tRate[i]) >> 18) + tPhase[i];
        const int u = (sl[idx & (SIN_N - 1)] + 512) >> 1;
        const int u2 = (u * u) >> 9;
        level[i] = static_cast<int16_t>((((u2 * u) >> 9) * amp) >> 9);
    }
    // At the defaults the spatial step is 18/16 sine entries per pixel and
    // the amplitude 210 field units. At Speed 50 the x wash advances
    // 27.78 px/s and the y wash 41.67 px/s in opposite directions; periods
    // are 32.768 s and 21.845333 s respectively.
    // Wash (p[5]) scales the amplitude: none at 0, 420 at 100. Wash
    // density (p[7]) sets the step in sixteenths of a sine entry per pixel:
    // 4 at 0 (a 4,096 px period, a near-uniform pulse), 18 at 50 (910 px)
    // and 64 at 100 (256 px, about two bands across the panel). The upper
    // half is steeper so the top of the slider shows distinct bands.
    const int washAmp = WASH * p[5] / 50;
    const uint32_t washStep = p[7] <= 50 ? 4u + p[7] * 14u / 50u : 18u + (p[7] - 50u) * 46u / 50u;
    for (int x = 0; x < w; ++x) {
        washCol[x] = static_cast<int16_t>((sl[(((static_cast<uint32_t>(x) * washStep) >> 4) - phW1) & (SIN_N - 1)] * washAmp) >> 9);
    }
    for (int y = 0; y < h; ++y) {
        const int wash = (sl[(((static_cast<uint32_t>(y) * washStep) >> 4) + phW2) & (SIN_N - 1)] * washAmp) >> 9;
        rowTerm[y] = static_cast<int16_t>(rowShade[y] + wash);
    }
    // Brightness (p[6]) moves the resting field: 0 at 0 (unlit tiles at
    // the palette's floor), 560 at 50, 1120 at 100.
    const int mid = MID * p[6] / 50;
    for (int ri = 0; ri < nRow; ++ri) {
        int16_t *dst = comb + static_cast<size_t>(ri) * combStride;
        const int lb = ri * nCol;
        for (int x = 0; x < w; ++x) {
            const int a = level[lb + cA[x]], b = level[lb + cB[x]];
            dst[x] = static_cast<int16_t>(mid + a + (((b - a) * cW[x]) >> 8) + colShade[x] + washCol[x]);
        }
    }
}

// bandRef is the page's final two loops fused: the scratch assignment is an
// exact int16 value, so its store/reload is unnecessary. Its Q8
// interpolation still rounds before adding the row term and ordered dither.
// With every slider in 0..100, a column entry is mid 0..1120 plus a level
// 0..3600 plus a dome of +/-380 plus a wash of +/-420: -800..5520. Seam
// differences are level differences only (both rows share the column dome
// and wash), at most 3600, products at most 921,600; row terms are
// -800..800, and dither is at most +/-256 (ditherAmp's cap is 16 indices).
// The final field is -1856..6576, so signed 16-bit PIE arithmetic cannot
// saturate accidentally. Both ends of the palette still need their clamp.
BGANIM_INLINE uint16_t mapPixel(int value, int rv, int d) {
    int v = (value + rv + d) >> 4;
    v = v < 0 ? 0 : (v > 255 ? 255 : v);
    return palette[v];
}

GM_ANIM_IRAM void bandRef(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int row = 0; row < rows; ++row) {
        const int y = y0 + row;
        const int code = rowBlend[y];
        const int wy = seamWeight[code & 15];
        const int16_t *a = comb + static_cast<size_t>(code >> 4) * combStride;
        const int16_t *b = wy ? a + combStride : a;
        const int rv = rowTerm[y];
        const int16_t *off = dith + (y & 7) * 8;
        int x = 0;
        // Only seam rows pay for the second column table and Q8 multiply.
        // Pair stores use BgAnim.h's four-byte destination alignment.
        if (wy == 0) {
            for (; x + 1 < w; x += 2) {
                const uint16_t p0 = mapPixel(a[x], rv, off[x & 7]);
                const uint16_t p1 = mapPixel(a[x + 1], rv, off[(x + 1) & 7]);
                *reinterpret_cast<uint32_t *>(dst) = static_cast<uint32_t>(p0) | (static_cast<uint32_t>(p1) << 16);
                dst += 2;
            }
        } else {
            for (; x + 1 < w; x += 2) {
                const int v0 = a[x] + (((b[x] - a[x]) * wy) >> 8);
                const int v1 = a[x + 1] + (((b[x + 1] - a[x + 1]) * wy) >> 8);
                const uint16_t p0 = mapPixel(v0, rv, off[x & 7]);
                const uint16_t p1 = mapPixel(v1, rv, off[(x + 1) & 7]);
                *reinterpret_cast<uint32_t *>(dst) = static_cast<uint32_t>(p0) | (static_cast<uint32_t>(p1) << 16);
                dst += 2;
            }
        }
        if (x < w) {
            const int value = a[x] + (((b[x] - a[x]) * wy) >> 8);
            *dst++ = mapPixel(value, rv, off[x & 7]);
        }
    }
}

#if GM_BGANIM_MOSAIC_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
// GCC 14.2 (-O2, xtensa-asm14.sh) baseline, read before this kernel:
// bandRef's flat loop is 31 instructions/pair inside LOOP; the seam loop
// spills wy, palette and its counter and uses a branch. Its gather schedule
// loads both colours before packing. Keep that schedule, interleave four
// independent gathers, and move the arithmetic into eight PIE lanes.
//
// The two bodies below share one loop per eight pixels. Flat rows cost 43
// instructions (5.375/pixel); seams cost 47 (5.875/pixel). The scalar gather
// and pair stores account for 36 of each body's instructions. Every scalar
// load has an independent instruction before use. PIE multiplies produce
// their result late: the next column load fills that gap, and row/dither
// addition fills the seam multiply's gap. The lookahead reads one unused
// vector on exit, covered by comb's explicit 16-byte guard. No PSRAM gather,
// per-pixel spill, or per-iteration branch remains.
//
// Q0/Q1: current source rows; Q2: result; Q3: row+dither; Q4: seam weight;
// Q5: zero; Q6: 4095 cap; Q7: 16. SAR stays 8, so multiply by Q7 also does
// the final >>4 without changing SAR in the loop. Clamping to 0..4095 before
// that shift equals the page's clamp to 0..255 afterwards. The probe in
// tools/qemubench/tests/anim_mosaic/probe executes min/max/subtract/signed
// multiply, including negative products. PIE subtraction can clamp negative
// overflow to -32767; the proven +/-4400 difference never approaches it.
//
// The compiler never allocates Q registers, so there is no Q clobber syntax.
// All scalar temporaries have early-clobber constraints; memory is clobbered.
// No CPENABLE write: FreeRTOS owns lazy coprocessor context activation.
// Source rows, dither and the local index buffer are 16-byte aligned by
// construction. Output only uses s32i and needs BgAnim.h's four-byte alignment.
// The scalar tail handles widths below eight and every non-multiple of eight.
// Counts are code shape, not a timing claim. Rung 4 still decides the default.
// clang-format off
#define MOSAIC_GATHER8 \
    "l16ui %[t0], %[idx], 0\n" \
    "l16ui %[t1], %[idx], 2\n" \
    "l16ui %[t2], %[idx], 4\n" \
    "l16ui %[t3], %[idx], 6\n" \
    "addx2 %[t0], %[t0], %[pal]\n" \
    "addx2 %[t1], %[t1], %[pal]\n" \
    "addx2 %[t2], %[t2], %[pal]\n" \
    "addx2 %[t3], %[t3], %[pal]\n" \
    "l16ui %[t0], %[t0], 0\n" \
    "l16ui %[t1], %[t1], 0\n" \
    "l16ui %[t2], %[t2], 0\n" \
    "l16ui %[t3], %[t3], 0\n" \
    "slli %[t1], %[t1], 16\n" \
    "slli %[t3], %[t3], 16\n" \
    "or %[t0], %[t0], %[t1]\n" \
    "or %[t2], %[t2], %[t3]\n" \
    "s32i %[t0], %[out], 0\n" \
    "s32i %[t2], %[out], 4\n" \
    "l16ui %[t0], %[idx], 8\n" \
    "l16ui %[t1], %[idx], 10\n" \
    "l16ui %[t2], %[idx], 12\n" \
    "l16ui %[t3], %[idx], 14\n" \
    "addx2 %[t0], %[t0], %[pal]\n" \
    "addx2 %[t1], %[t1], %[pal]\n" \
    "addx2 %[t2], %[t2], %[pal]\n" \
    "addx2 %[t3], %[t3], %[pal]\n" \
    "l16ui %[t0], %[t0], 0\n" \
    "l16ui %[t1], %[t1], 0\n" \
    "l16ui %[t2], %[t2], 0\n" \
    "l16ui %[t3], %[t3], 0\n" \
    "slli %[t1], %[t1], 16\n" \
    "slli %[t3], %[t3], 16\n" \
    "or %[t0], %[t0], %[t1]\n" \
    "or %[t2], %[t2], %[t3]\n" \
    "s32i %[t0], %[out], 8\n" \
    "s32i %[t2], %[out], 12\n"

// BEGIN VERBATIM QEMU KERNEL
GM_ANIM_IRAM __attribute__((noinline)) void mosaicRowAsm(uint16_t *out, const int16_t *a, const int16_t *b,
                                                        const int16_t *off, const uint16_t *pal,
                                                        int rv, int wy, int n) {
    const int groups = n / 8;
    if (groups > 0) {
        uint16_t indices[8] __attribute__((aligned(16)));
        uint32_t rowWord = (uint16_t)rv;
        rowWord |= rowWord << 16;
        const uint32_t weightWord = (uint32_t)wy | ((uint32_t)wy << 16);
        int t0, t1, t2, t3;
        asm volatile(
            "ee.vld.128.ip q3, %[off], 0\n"
            "ee.movi.32.q q4, %[row], 0\n"
            "ee.movi.32.q q4, %[row], 1\n"
            "ee.movi.32.q q4, %[row], 2\n"
            "ee.movi.32.q q4, %[row], 3\n"
            "ee.vadds.s16 q3, q3, q4\n"
            "ee.zero.q q5\n"
            "movi %[t0], -1\n"
            "extui %[t0], %[t0], 0, 12\n" // 4095, the maximum Q4 field before >>4
            "slli %[t1], %[t0], 16\n"
            "or %[t0], %[t0], %[t1]\n"
            "ee.movi.32.q q6, %[t0], 0\n"
            "ee.movi.32.q q6, %[t0], 1\n"
            "ee.movi.32.q q6, %[t0], 2\n"
            "ee.movi.32.q q6, %[t0], 3\n"
            "movi %[t0], 16\n" // (field * 16) >> SAR(8) equals field >> 4
            "slli %[t1], %[t0], 16\n"
            "or %[t0], %[t0], %[t1]\n"
            "ee.movi.32.q q7, %[t0], 0\n"
            "ee.movi.32.q q7, %[t0], 1\n"
            "ee.movi.32.q q7, %[t0], 2\n"
            "ee.movi.32.q q7, %[t0], 3\n"
            "ssai 8\n"
            "beqz %[wy], 3f\n"
            "ee.movi.32.q q4, %[wy], 0\n"
            "ee.movi.32.q q4, %[wy], 1\n"
            "ee.movi.32.q q4, %[wy], 2\n"
            "ee.movi.32.q q4, %[wy], 3\n"
            "ee.vld.128.ip q0, %[a], 16\n"
            "ee.vld.128.ip q1, %[b], 16\n"
            "loopnez %[n], 1f\n"
            "ee.vsubs.s16 q2, q1, q0\n"
            "ee.vmul.s16 q2, q2, q4\n"
            "ee.vadds.s16 q0, q0, q3\n"
            "ee.vadds.s16 q2, q2, q0\n"
            "ee.vmax.s16 q2, q2, q5\n"
            "ee.vmin.s16 q2, q2, q6\n"
            "ee.vmul.s16 q2, q2, q7\n"
            "ee.vld.128.ip q0, %[a], 16\n"
            "ee.vst.128.ip q2, %[idx], 0\n"
            "ee.vld.128.ip q1, %[b], 16\n"
            MOSAIC_GATHER8
            "addi %[out], %[out], 16\n"
            "1:\n"
            "addi %[b], %[b], -16\n" // rewind the unused lookahead before the scalar tail
            "j 4f\n"
            "3:\n"
            "ee.vld.128.ip q0, %[a], 16\n"
            "loopnez %[n], 2f\n"
            "ee.vadds.s16 q2, q0, q3\n"
            "ee.vmax.s16 q2, q2, q5\n"
            "ee.vmin.s16 q2, q2, q6\n"
            "ee.vmul.s16 q2, q2, q7\n"
            "ee.vld.128.ip q0, %[a], 16\n"
            "ee.vst.128.ip q2, %[idx], 0\n"
            MOSAIC_GATHER8
            "addi %[out], %[out], 16\n"
            "2:\n"
            "4:\n"
            "addi %[a], %[a], -16\n"
            : [out] "+&r"(out), [a] "+&r"(a), [b] "+&r"(b),
              [t0] "=&r"(t0), [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3)
            : [off] "r"(off), [row] "r"(rowWord), [wy] "r"(weightWord), [pal] "r"(pal),
              [n] "r"(groups), [idx] "r"(indices)
            : "memory");
    }
    for (int x = 0; x < n % 8; ++x) {
        const int value = wy ? a[x] + (((b[x] - a[x]) * wy) >> 8) : a[x];
        int v = (value + rv + off[x]) >> 4; // groups always end at Bayer x phase zero
        v = v < 0 ? 0 : (v > 255 ? 255 : v);
        out[x] = pal[v];
    }
}
// END VERBATIM QEMU KERNEL
#undef MOSAIC_GATHER8
// clang-format on
#endif

GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
#if GM_BGANIM_MOSAIC_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
    for (int row = 0; row < rows; ++row) {
        const int y = y0 + row;
        const int code = rowBlend[y];
        const int wy = seamWeight[code & 15];
        const int16_t *a = comb + static_cast<size_t>(code >> 4) * combStride;
        const int16_t *b = wy ? a + combStride : a;
        mosaicRowAsm(dst + static_cast<size_t>(row) * w, a, b, dith + (y & 7) * 8, palette, rowTerm[y], wy, w);
    }
#else
    bandRef(dst, y0, rows, w, tMs, p);
#endif
}

void release() {
    releaseTable(comb, (static_cast<size_t>(combRows) * combStride + 8) * sizeof(int16_t));
    releaseTable(rowTerm, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(rowBlend, static_cast<size_t>(allocH));
    releaseTable(seamWeight, 16 * sizeof(uint16_t));
    releaseTable(dith, 64 * sizeof(int16_t));
    releaseTable(palette, 256 * sizeof(uint16_t));
    releaseTable(colShade, static_cast<size_t>(allocW) * sizeof(int16_t));
    releaseTable(rowShade, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(washCol, static_cast<size_t>(allocW) * sizeof(int16_t));
    releaseTable(cA, static_cast<size_t>(allocW));
    releaseTable(cB, static_cast<size_t>(allocW));
    releaseTable(cW, static_cast<size_t>(allocW) * sizeof(uint16_t));
    releaseTable(level, TILE_COUNT * sizeof(int16_t));
    releaseTable(tPhase, TILE_COUNT * sizeof(uint16_t));
    releaseTable(tRate, TILE_COUNT);
    releaseTable(ramp, 256 * sizeof(uint16_t));
    sl = nullptr;
    allocW = allocH = combStride = combRows = 0;
    nCol = nRow = 1;
    lastSize = lastVariation = lastBevel = -1;
    lastThemeGen = 0;
    paletteValid = false;
}

} // namespace

extern const BgAnimation bg_anim_mosaic;
const BgAnimation bg_anim_mosaic = {
    "mosaic",
    "Mosaic",
    {{"speed", "Speed", 50},
     {"size", "Tile size", 45},
     {"contrast", "Contrast", 30},
     {"variation", "Variation", 55},
     {"bevel", "Bevel", 50},
     {"wash", "Wash", 50},
     {"brightness", "Brightness", 50},
     {"washdensity", "Wash density", 50}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
