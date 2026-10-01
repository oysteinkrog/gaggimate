#ifndef GAGGIMATE_SIM

// "Dimples": a matte surface pressed into a lattice of shallow round dimples,
// lit by one light that circles the panel once every 7.5 seconds at Speed 50
// (a 24 second turn scaled by RATE_CAL in frame()). Nothing in the picture
// moves. What changes is the shading, so the rims of the dimples brighten
// and darken as the light goes round, and the whole sheet breathes as the
// light's height rises and falls on an 11.6 second cycle. This is entry
// 42, id 'dimples', in tools/animbench/web/anim_bench.html, including the
// softened design in that entry's comment: a raised cosine bell of radius 18
// and depth 9 on a staggered lattice rather than the candidate's cubic on a
// square one, a texel that covers four screen pixels so the dimples sit
// 128 px apart, and the index window opened to 32..150 so highlights have
// somewhere to go.
//
// The tile is 64x64 texels of surface normal, built once in init() and never
// touched again: the relief is fixed, only the light moves. Normals are
// signed amplitude 127, which is what makes the per texel lighting an integer
// dot product. frame() does all of the lighting, 4,096 dot products, and
// writes one palette index per texel into tex. Nothing per pixel is left but
// the two gathers the design asks for: the texel's index and the palette.
//
// Fixed point: normals are Q7 (127 is one unit), the light vector is in the
// same units scaled by 256, and the dot product is shifted right by 8 to come
// back to index units, which is the page's own arithmetic. The shift is
// arithmetic on a negative sum, as JS >> is. The index window keeps tex inside
// 32..150 and the Bayer offsets are -2..2, so the sum a pixel looks up is
// always inside 30..152 and the page's clamp cannot fire. The kernel relies on
// that and carries no clamp.
//
// The page scales the theme ramp by its own Brightness parameter, so the port
// does too: buildThemeRamp at 180 + round(bright * 0.76). Most of the fleet
// leaves the ramp alone and lets setThemeTone own brightness. Kaleido set the
// precedent for following the page here.
//
// Dither is a fixed 8x8 Bayer of amplitude 1.5 in palette index units. Two
// roundings are in play and they disagree here, so each follows the page.
// The page's bayerOffsets goes through its own lround helper, which rounds a
// half away from zero as C's lround does, and at this amplitude four cells
// land exactly on a half; the two negative ones are where Math.round would
// give a different offset. Everything else the page rounds, the normals, the
// light vector and the ramp scale, goes through Math.round, which rounds a
// half toward positive infinity, so those use floor(v + 0.5).
//
// Parameters. Speed, Relief and Brightness came with the port; the other
// five were added on 2026-10-01 (gm-3vj.45). Each new one defaults to 50,
// and at 50 it reproduces the constant this file used to hard-code, so the
// default picture is the old one bit for bit. All five act in frame(), on
// the light vector or on the per texel index, so bandRef and the kernel are
// untouched and the 32..150 window the kernel relies on still holds.
//   p3 Light height  the light's mean height, 30..130 (80 at 50). Low light
//                    leaves the flat sheet dark and only the rims that face
//                    the light lit; high light brightens the whole sheet.
//   p4 Height swing  how far the height breathes, 0..44 (22 at 50).
//   p5 Base tone     the flat surface's index offset, 20..60 (40 at 50), so
//                    the whole sheet darkens or lightens under the shading.
//   p6 Contrast      the shading's distance from the flat surface's lit
//                    level, scaled by 1/8 at 0, 1 at 50 and 3 at 100. At 0
//                    the dimples nearly vanish; at 100 the rims clip to the
//                    window's ends.
//   p7 Orbit shape   the light's path, a line along x at 0, the circle at
//                    50 and an ellipse twice as tall as wide at 100, so the
//                    top and bottom rims light less or more than the sides.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

#if defined(ESP_PLATFORM)
#include <esp_attr.h>
#define GM_ANIM_IRAM IRAM_ATTR
#else
#define GM_ANIM_IRAM
#endif

#ifndef GM_BGANIM_DIMPLES_ASM
// Device timing is still required. The portable reference stays reachable
// through /api/debug/animtest and useref=1 for the production parity rung.
#define GM_BGANIM_DIMPLES_ASM 1
#endif

namespace {
using namespace bganim;

constexpr int TS = 64;               // tile side in texels
constexpr int TPX = 2;               // texel footprint, 1 << TPX screen pixels
constexpr int TILE_PX = TS << TPX;   // 256: a row repeats every 256 pixels
constexpr int DIMPLE_R = 18;         // bell radius in texels, neighbours overlap
constexpr int DIMPLE_H = 9;          // bell depth in height units
// The design's constants, now the defaults of Base tone, Light height and
// Height swing: 40, 80 and 22 at parameter 50.
constexpr int TURN_MS = 24000;       // the light circles once every 24 s
constexpr int LZ_PERIOD_MS = 37000;  // the light's height breathes on 37 s
constexpr int IDX_LO = 32, IDX_HI = 150;
// Staggered lattice: two rows of two, offset half a period in x.
constexpr int DIMPLES[4][2] = {{8, 8}, {40, 8}, {24, 40}, {56, 40}};

int16_t *nrm = nullptr;      // PSRAM, 24,576 B: Nx, Ny, Nz interleaved per texel
uint8_t *tex = nullptr;      // slab, 4,096 B: one palette index per texel
uint16_t *palette = nullptr; // slab, 512 B: the theme ramp
int16_t *dith = nullptr;     // slab, 128 B: eight Bayer rows of index offsets

int lastBright = -1;
uint32_t lastThemeGen = 0xFFFFFFFFu;

constexpr size_t NRM_BYTES = 3u * TS * TS * sizeof(int16_t);
constexpr size_t TEX_BYTES = TS * TS;
constexpr size_t PAL_BYTES = 256 * sizeof(uint16_t);
constexpr size_t DITH_BYTES = 64 * sizeof(int16_t);
constexpr size_t HEIGHT_BYTES = TS * TS * sizeof(double);
// 4,736 B of the 9,216 B animation slab, at every render resolution: the tile
// is in texels, so nothing here is sized from w or h. tex is read once per
// four pixels, the palette once per pixel and the dither once per pixel, so
// all three belong in the slab. The normals are read 4,096 times per frame in
// one sequential sweep and never per pixel, which is the case alloc()'s PSRAM
// streams as fast as SRAM would.
static_assert(TEX_BYTES + PAL_BYTES + DITH_BYTES <= HOT_SLAB_BYTES - HOT_SHARED_RESERVE,
              "Dimples' hot tables must fit the per-animation slab");

void release();

// JS Math.round: a half goes toward positive infinity, which floor(v + 0.5)
// reproduces for negative values as well. The page rounds normals, the light
// vector and the dither offsets, and all three can be negative.
int jsRound(double v) { return static_cast<int>(floor(v + 0.5)); }

bool init(int w, int h) {
    if (w <= 0 || h <= 0) {
        release();
        return false;
    }
    if (tex != nullptr) return true; // resolution independent, so nothing to resize
    release();
    tex = static_cast<uint8_t *>(allocHot(TEX_BYTES));
    palette = static_cast<uint16_t *>(allocHot(PAL_BYTES));
    dith = static_cast<int16_t *>(allocHot(DITH_BYTES));
    nrm = static_cast<int16_t *>(alloc(NRM_BYTES));
    if (tex == nullptr || palette == nullptr || dith == nullptr || nrm == nullptr) {
        release();
        return false;
    }
    // The height field is scratch: the normals are what the frame loop reads.
    // Doubles, and the page's own order of operations, so the rounded normals
    // match the preview bit for bit.
    double *hgt = static_cast<double *>(alloc(HEIGHT_BYTES));
    if (hgt == nullptr) {
        release();
        return false;
    }
    for (int k = 0; k < TS * TS; k++) hgt[k] = 0.0;
    for (int d = 0; d < 4; d++) {
        const int cx = DIMPLES[d][0], cy = DIMPLES[d][1];
        for (int ty = 0; ty < TS; ty++) {
            for (int tx = 0; tx < TS; tx++) {
                // Wrapped to the nearest image of the dimple, so the tile has
                // no seam. The bells of neighbouring dimples reach each other
                // and their depths add, which is what makes this read as one
                // pressed sheet rather than a grid of punched holes.
                int dx = tx - cx;
                if (dx > TS / 2) dx -= TS;
                if (dx < -TS / 2) dx += TS;
                int dy = ty - cy;
                if (dy > TS / 2) dy -= TS;
                if (dy < -TS / 2) dy += TS;
                const int r2 = dx * dx + dy * dy;
                if (r2 >= DIMPLE_R * DIMPLE_R) continue;
                const double u = sqrt(static_cast<double>(r2)) / DIMPLE_R;
                hgt[ty * TS + tx] += -DIMPLE_H * 0.5 * (1.0 + cos(M_PI * u));
            }
        }
    }
    for (int ty = 0; ty < TS; ty++) {
        for (int tx = 0; tx < TS; tx++) {
            const int xp = (tx + 1) & (TS - 1), xm = (tx + TS - 1) & (TS - 1);
            const int yp = (ty + 1) & (TS - 1), ym = (ty + TS - 1) & (TS - 1);
            const double hx = (hgt[ty * TS + xp] - hgt[ty * TS + xm]) * 0.5;
            const double hy = (hgt[yp * TS + tx] - hgt[ym * TS + tx]) * 0.5;
            const double len = sqrt(hx * hx + hy * hy + 1.0);
            int16_t *n = nrm + 3 * (ty * TS + tx);
            n[0] = static_cast<int16_t>(jsRound(-hx / len * 127.0));
            n[1] = static_cast<int16_t>(jsRound(-hy / len * 127.0));
            n[2] = static_cast<int16_t>(jsRound(1.0 / len * 127.0));
        }
    }
    releaseTable(hgt, HEIGHT_BYTES);
    // Constant: the amplitude follows neither the theme nor the parameters, so
    // this is built once where the page rebuilds it with the palette. lround,
    // not jsRound, because the page's bayerOffsets rounds a half away from
    // zero. The result is -2..2, and 1.5 / 31.5 is one double, evaluated the
    // way the page evaluates amp * unit / 31.5.
    for (int k = 0; k < 64; k++) {
        dith[k] = static_cast<int16_t>(lround((BAYER8[k] - 31.5) * (1.5 / 31.5)));
    }
    return true;
}

void frame(uint32_t tMs, int, int, const uint8_t p[BG_ANIM_PARAMS]) {
    const uint32_t gen = themeGen();
    if (lastBright != p[2] || lastThemeGen != gen) {
        // The page's own ramp scale. Integer round(p[2] * 76 / 100) matches its
        // Math.round(p[2] * 76 / 100) exactly: 76 * p never lands on a half.
        buildThemeRamp(palette, static_cast<uint16_t>(180 + (static_cast<int>(p[2]) * 76 + 50) / 100));
        lastBright = p[2];
        lastThemeGen = gen;
    }
    // Time stays a pure function of tMs, speed changes included. Q24 carries
    // the float speed multiplier and the 64-bit product stays below 2^61 even
    // at millis() wrap. Each cycle is reduced before the conversion to double,
    // so days of uptime do not erase phase precision the way the page's own
    // t * 2 * pi / period does. At the bench frame times the two agree.
    // RATE_CAL is the Speed calibration (bead gm-kh2s): 3.2x the rate this
    // entry was designed at, so Speed 50 gives the fleet's target movement.
    // The light circles in 7.5 s and its height breathes on 11.6 s at 50.
    constexpr float RATE_CAL = 3.2f;
    const uint32_t speedQ24 = static_cast<uint32_t>(speedMul(p[0]) * RATE_CAL * 16777216.0f);
    const uint64_t ttQ24 = static_cast<uint64_t>(tMs) * speedQ24;
    const double ang = static_cast<double>(ttQ24 % (static_cast<uint64_t>(TURN_MS) << 24)) *
                       (6.283185307179586 / (static_cast<double>(TURN_MS) * 16777216.0));
    const double lzAng = static_cast<double>(ttQ24 % (static_cast<uint64_t>(LZ_PERIOD_MS) << 24)) *
                         (6.283185307179586 / (static_cast<double>(LZ_PERIOD_MS) * 16777216.0));
    // Relief rides on the light's lateral reach, which is the same thing as
    // scaling the normal's xy and costs nothing per texel. 55..125.
    const int lr = 55 + (static_cast<int>(p[1]) * 70 + 50) / 100;
    // Integer round(p * k / 100) equals the page's Math.round for every p in
    // 0..100, since the numerator is never negative. At p = 50 these give
    // exactly the old constants, and orbit is exactly 1.0, so the products
    // below are the old ones.
    const int base = 20 + (static_cast<int>(p[5]) * 40 + 50) / 100;
    const int lzMid = 30 + static_cast<int>(p[3]);
    const int lzSwing = (static_cast<int>(p[4]) * 44 + 50) / 100;
    const int c = p[6] <= 50 ? 4 + (static_cast<int>(p[6]) * 28) / 50 : 32 + ((static_cast<int>(p[6]) - 50) * 64) / 50;
    const double orbit = static_cast<double>(p[7]) / 50.0;
    const int lx = jsRound(lr * cos(ang));
    const int ly = jsRound(lr * orbit * sin(ang));
    int lz = lzMid + jsRound(lzSwing * sin(lzAng));
    // Low height plus full swing would put the light under the sheet. The
    // floor never fires at the defaults, where lz stays inside 58..102.
    if (lz < 8) lz = 8;
    // The flat surface's lit level, Nz = 127 times lz, in index units. The
    // contrast scale works about it in Q5, and at c = 32 the + 16 rounding
    // leaves the old d unchanged.
    const int flat = (127 * lz) >> 8;
    // 4,096 dot products, three products each. This stays scalar on purpose:
    // the page's sum is exact in 32 bits before the shift, and the largest
    // term reaches 44,700, so an int16 vector sum would saturate and shifting
    // each product before the sum would not give the same answer.
    const int16_t *n = nrm;
    for (int k = 0; k < TS * TS; k++, n += 3) {
        const int d = (n[0] * lx + n[1] * ly + n[2] * lz) >> 8;
        int v = base + flat + (((d - flat) * c + 16) >> 5);
        if (v < IDX_LO) {
            v = IDX_LO;
        } else if (v > IDX_HI) {
            v = IDX_HI;
        }
        tex[k] = static_cast<uint8_t>(v);
    }
}

// The portable specification, one pixel at a time, exactly the page's loop.
// No float, no division and no libm call happens per pixel.
GM_ANIM_IRAM void bandRef(uint16_t *__restrict dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int row = 0; row < rows; row++) {
        const int y = y0 + row;
        const uint8_t *trow = tex + ((y >> TPX) & (TS - 1)) * TS;
        const int16_t *dr = dith + (y & 7) * 8;
        uint16_t *out = dst + static_cast<size_t>(row) * w;
        for (int x = 0; x < w; x++) {
            int i = trow[(x >> TPX) & (TS - 1)] + dr[x & 7];
            i = i < 0 ? 0 : i > 255 ? 255 : i;
            out[x] = palette[i];
        }
    }
}

#if GM_BGANIM_DIMPLES_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
// What GCC 14 makes of bandRef's inner loop is 15 instructions per pixel:
// two extui and an add for the texel address, an extui and an addx2 for the
// dither address, l8ui and l16si, the add, the min and max of the clamp, an
// extui, addx2 and l16ui for the colour, s16i, and two pointer increments. It
// already uses LOOP and it already hides both load latencies, so there is
// nothing left to win by rescheduling it. What it cannot do is skip work: it
// pays the address arithmetic, the dither lookup and the clamp on every one
// of the 480 pixels in a row.
//
// The edge the compiler cannot take is the row's period. A pixel's colour
// depends on (x >> 2) & 63 and on x & 7, so the row repeats every 256 pixels
// exactly, whatever the width and whatever the frame state. So the kernel
// gathers 256 pixels and copies the rest, which at 480 wide turns 224 of the
// 480 gathers into 28 vector moves. A whole 480 pixel row is about 1,230
// instructions, 2.6 per pixel, against GCC's 7,200.
//
// The 256 gathers themselves are two passes of 32 cells. A cell is one texel,
// four pixels wide, and its four dither phases are 0..3 in an even cell and
// 4..7 in an odd one, so a pass over the cells of one parity needs only four
// of the eight phases live. Each phase is a palette base biased by its own
// offset, pal + 2 * dr[j], so the per pixel index add and the page's clamp
// both disappear into the base: the bias is legal because tex is clamped to
// 32..150 in frame() and the offsets are -2..2. One texel load, four biased
// gathers and two packed 32-bit stores cover four pixels.
//
// PIE does the copy and nothing else. There is no vector gather on this unit,
// and the arithmetic that would have vectorised is already gone into the
// frame loop and the biased bases. The compiler does not allocate q0-q7 and
// has no clobber syntax for them, so this block owns q0. It never writes
// CPENABLE: FreeRTOS enables CP3 lazily per task, and band() runs on the
// render task, never in an ISR.
//
// These are instruction counts and a stall-free dependency schedule, not
// measured device cycles. Device parity and production timing are still the
// rung that decides whether this flag stays on.

// BEGIN VERBATIM PRODUCTION KERNELS
// One cell per iteration: 17 instructions for four pixels. Every load has two
// independent instructions between it and its consumer, so no load-use
// interlock is left, and every address is computed at least three
// instructions ahead of the load that uses it. b0..b3 are byte addresses of
// pal biased by the cell's four dither offsets, so a texel value indexes them
// directly. out advances eight pixels because the next cell of this parity is
// eight pixels along, and tp advances two texels for the same reason.
// out must be 4-byte aligned, which is BgAnim.h's precondition on dst.
GM_ANIM_IRAM __attribute__((noinline)) void dimplesCellsAsm(uint16_t *out, const uint8_t *tp,
                                                            int b0, int b1, int b2, int b3, int cells) {
    if (cells <= 0) return;
    int t, v0, v1, v2, v3;
    asm volatile("loopnez %[cells], 1f\n"
                 "l8ui   %[t], %[tp], 0\n"
                 "addi   %[tp], %[tp], 2\n"
                 "addx2  %[v0], %[t], %[b0]\n"
                 "addx2  %[v1], %[t], %[b1]\n"
                 "addx2  %[v2], %[t], %[b2]\n"
                 "addx2  %[v3], %[t], %[b3]\n"
                 "l16ui  %[v0], %[v0], 0\n"
                 "l16ui  %[v1], %[v1], 0\n"
                 "l16ui  %[v2], %[v2], 0\n"
                 "l16ui  %[v3], %[v3], 0\n"
                 "slli   %[v1], %[v1], 16\n"
                 "or     %[v0], %[v0], %[v1]\n"
                 "slli   %[v3], %[v3], 16\n"
                 "s32i   %[v0], %[out], 0\n"
                 "or     %[v2], %[v2], %[v3]\n"
                 "s32i   %[v2], %[out], 4\n"
                 "addi   %[out], %[out], 16\n"
                 "1:\n"
                 : [out] "+&r"(out), [tp] "+&r"(tp), [t] "=&r"(t), [v0] "=&r"(v0), [v1] "=&r"(v1),
                   [v2] "=&r"(v2), [v3] "=&r"(v3)
                 : [b0] "r"(b0), [b1] "r"(b1), [b2] "r"(b2), [b3] "r"(b3), [cells] "r"(cells)
                 : "memory");
}

// The periodic extension of a row: dst is src plus one whole tile of pixels.
// A scalar prefix takes dst to a 16-byte boundary, and src moves with it, so
// both are aligned once the vector loop starts. VLD and VST mask the low four
// address bits instead of trapping, so the prefix is what keeps them off a
// neighbouring row. The 512-byte gap between src and dst is wider than the
// 16-byte block, so a forward copy never reads a byte this call has just
// written, which is what makes one call correct even past two tiles.
GM_ANIM_IRAM __attribute__((noinline)) void dimplesCopyAsm(uint16_t *dst, const uint16_t *src, int n) {
    if (n <= 0) return;
    if (((reinterpret_cast<uintptr_t>(dst) ^ reinterpret_cast<uintptr_t>(src)) & 15u) != 0) {
        for (int i = 0; i < n; i++) dst[i] = src[i];
        return;
    }
    while (n > 0 && (reinterpret_cast<uintptr_t>(dst) & 15u) != 0) {
        *dst++ = *src++;
        --n;
    }
    const int blocks = n >> 3;
    if (blocks != 0) {
        asm volatile("loopnez %[n], 1f\n"
                     "ee.vld.128.ip q0, %[src], 16\n"
                     "ee.vst.128.ip q0, %[dst], 16\n"
                     "1:\n"
                     : [dst] "+&r"(dst), [src] "+&r"(src)
                     : [n] "r"(blocks)
                     : "memory");
    }
    for (int i = 0; i < (n & 7); i++) dst[i] = src[i];
}

// Plain arguments so the whole row split runs unchanged in the QEMU test.
// The tail loop covers a width that is not a whole number of cells, which is
// the 233 px half-resolution row of the 466 px panel.
GM_ANIM_IRAM __attribute__((noinline)) void dimplesRowAsm(uint16_t *out, const uint8_t *trow,
                                                          const uint16_t *pal, const int16_t *dr, int w) {
    const int n0 = w < TILE_PX ? w : TILE_PX;
    const int cells = n0 >> TPX;
    int base[8];
    // Byte addresses, not uint16_t pointers: a negative offset would put a
    // pointer before the palette, and the kernel only ever adds a texel value
    // of 32 or more to it.
    for (int j = 0; j < 8; j++) {
        base[j] = static_cast<int>(reinterpret_cast<uintptr_t>(pal)) + 2 * dr[j];
    }
    dimplesCellsAsm(out, trow, base[0], base[1], base[2], base[3], (cells + 1) >> 1);
    if (cells > 1) {
        dimplesCellsAsm(out + 4, trow + 1, base[4], base[5], base[6], base[7], cells >> 1);
    }
    for (int x = cells << TPX; x < n0; x++) {
        out[x] = pal[trow[(x >> TPX) & (TS - 1)] + dr[x & 7]];
    }
    if (w > TILE_PX) dimplesCopyAsm(out + TILE_PX, out, w - TILE_PX);
}
// END VERBATIM PRODUCTION KERNELS

GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    // Only absolute y picks the texel row and the Bayer phase, so a lone row
    // in either interlace parity renders the same pixels it would in a pair.
    for (int row = 0; row < rows; row++) {
        const int y = y0 + row;
        dimplesRowAsm(dst + static_cast<size_t>(row) * w, tex + ((y >> TPX) & (TS - 1)) * TS, palette,
                      dith + (y & 7) * 8, w);
    }
}
#else
// Host and explicitly disabled builds run the page's portable reference.
GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
    bandRef(dst, y0, rows, w, tMs, p);
}
#endif

void release() {
    releaseTable(dith, DITH_BYTES);
    releaseTable(palette, PAL_BYTES);
    releaseTable(tex, TEX_BYTES);
    releaseTable(nrm, NRM_BYTES);
    lastBright = -1;
    lastThemeGen = 0xFFFFFFFFu;
}

} // namespace

extern const BgAnimation bg_anim_dimples;
const BgAnimation bg_anim_dimples = {
    "dimples",
    "Dimples",
    {{"speed", "Speed", 50},
     {"relief", "Relief", 58},
     {"bright", "Brightness", 62},
     {"height", "Light height", 50},
     {"swing", "Height swing", 50},
     {"tone", "Base tone", 50},
     {"contrast", "Contrast", 50},
     {"orbit", "Orbit shape", 50}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
