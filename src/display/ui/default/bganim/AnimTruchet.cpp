#ifndef GAGGIMATE_SIM

// "Truchet": broad quarter-circle paths on a dark ground, drifting diagonally.
// This is entry 30, id 'truchet', in tools/animbench/web/anim_bench.html:
// two 128x128 tiles, a seeded orientation hash and five tile-row copies per
// scanline. The approved entry's deviations are part of the design: radius
// 64, half width 16..34 pixels, and scroll 18/12 pixels per second at speed 50.
// Coordinates increase as time advances, so the visible paths move up-left,
// unless Drift angle turns the scroll vector somewhere else.
//
// Eight user parameters, all of them acting in frame(): Speed and Drift angle
// on the two scroll accumulators, Arc width, Glow and Sharpness on the two
// tiles, Tile bias on the 256-entry orientation table, Contrast on the
// palette and Grain on the dither table. Nothing a parameter does reaches the
// pixel loop or either Xtensa kernel, and at the defaults every derived value
// is the constant it replaced, bit for bit.
//
// Tile values retain the page's Q4 palette indices, including its pixel-centre
// sampling and raised-cosine shoulders. Quantising these to bytes before the
// Bayer add would lose the fractional coverage the page actually renders.
// The two tiles therefore stream from PSRAM as 65,536 B of uint16 data, while
// the assembled row, palette, orientation hash and dither stay in the slab.
// No float work or libm call occurs in bandRef() or the device band().

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>
#include <string.h>

#ifndef GM_BGANIM_TRUCHET_ASM
// Device timing is still required. The portable reference remains available
// to /api/debug/animtest and useref=1 for the production parity/timing rung.
#define GM_BGANIM_TRUCHET_ASM 1
#endif

namespace {
using namespace bganim;

constexpr int TILE = 128;
constexpr int HALF = TILE / 2;
constexpr int CELLS = 5;
constexpr int SCAN = CELLS * TILE; // 640 covers x + subX at every width <= 480
constexpr int GROUND = 26;        // darkest arc-free position in the theme ramp
constexpr size_t TILE_BYTES = 2 * TILE * TILE * sizeof(uint16_t);
constexpr size_t TILE_ALLOC = TILE_BYTES + 15; // align the PSRAM owner up to 16 B

uint8_t *tileOwner = nullptr; // PSRAM allocation owner; tiles below is an alias
uint16_t *tiles = nullptr;    // two orientations, 32,768 B each, row stride 256 B
uint16_t *scan = nullptr;     // 1,280 B, slab: assembled Q4 scanline
uint16_t *palette = nullptr;  // 512 B, slab: direct 256-entry theme ramp
uint8_t *orient = nullptr;    // 256 B, slab: full mulberry32 output bytes
uint8_t *obit = nullptr;      // 256 B, slab: the orientation bit Tile bias picked
int16_t *dith = nullptr;      // 128 B, slab: eight Bayer rows, Q4 index offsets
int16_t *dithScan = nullptr;  // 128 B, slab: same rows rotated by -subX

uint32_t lastThemeGen = 0;
int lastArc = -1, lastGlow = -1, lastSoft = -1;
int lastContrast = -1, lastGlowPal = -1, lastGrain = -1, lastBias = -1;
bool paletteValid = false;
uint32_t scrollX = 0, scrollY = 0;

// The scroll accumulators are unsigned, and Drift angle can point the scroll
// vector backwards, so both carry this bias. It is a multiple of 256 * TILE,
// which leaves the orientation hash alone (the hash takes cellX * 7 and
// cellY * 13 modulo 256, and 256 cells of stride change both by a multiple of
// 256), a multiple of TILE, which leaves subX and ty alone, and a multiple of
// 8, which leaves the dither phase alone. So it is invisible in the output.
constexpr uint32_t SCROLL_BIAS = 0x40000000u;
static_assert(SCROLL_BIAS % (256u * TILE) == 0, "SCROLL_BIAS must not move the orientation hash");

// 2,560 B of the 9,216 B animation slab, independent of render resolution.
// The tiles are bulk sequential input, not per-pixel PSRAM gathers. Their
// aligned alias is never passed to releaseTable(), only the original owner.
void release();

bool init(int, int) {
    if (tileOwner == nullptr) {
        tileOwner = static_cast<uint8_t *>(alloc(TILE_ALLOC));
        if (tileOwner == nullptr) {
            release();
            return false;
        }
        tiles = reinterpret_cast<uint16_t *>((reinterpret_cast<uintptr_t>(tileOwner) + 15u) & ~uintptr_t(15));
    }
    if (scan == nullptr) scan = static_cast<uint16_t *>(allocHot(SCAN * sizeof(uint16_t)));
    if (palette == nullptr) palette = static_cast<uint16_t *>(allocHot(256 * sizeof(uint16_t)));
    if (orient == nullptr) {
        orient = static_cast<uint8_t *>(allocHot(256));
        if (orient != nullptr) {
            // Exact mulberry32(0x7ce1), not the shared xorshift generator.
            // JS Math.imul and >>> use modulo-2^32 arithmetic at every step.
            uint32_t seed = 0x7ce1;
            for (int i = 0; i < 256; i++) {
                seed += 0x6d2b79f5u;
                uint32_t v = (seed ^ (seed >> 15)) * (1u | seed);
                v = (v + (v ^ (v >> 7)) * (61u | v)) ^ v;
                orient[i] = static_cast<uint8_t>((v ^ (v >> 14)) >> 24);
            }
        }
    }
    if (obit == nullptr) obit = static_cast<uint8_t *>(allocHot(256));
    if (dith == nullptr) dith = static_cast<int16_t *>(allocHot(64 * sizeof(int16_t)));
    if (dithScan == nullptr) dithScan = static_cast<int16_t *>(allocHot(64 * sizeof(int16_t)));
    if (scan == nullptr || palette == nullptr || orient == nullptr || obit == nullptr || dith == nullptr ||
        dithScan == nullptr) {
        release();
        return false;
    }
    return true;
}

// Contrast, Q8 gain about the picture's own midpoint. 256 at the default, so
// the remapped palette index is the plain index and the ramp is copied entry
// for entry. 0.25x at 0 flattens the arcs into the ground, 2.5x at 100 drives
// the ground to the darkest stop and the arc cores to the brightest.
int contrastQ8(uint8_t v) {
    const int s = v;
    return s < 50 ? 64 + s * 192 / 50 : 256 + (s - 50) * 384 / 50;
}

// Grain, a gain on the Bayer amplitude. Exactly 1.0f at the default: the
// second branch multiplies zero by the step. 0 removes the dither and shows
// the ramp's own steps inside the arc shoulders; the top of the range is
// 1.6x, which is what the kernel's index proof below has room for.
float grainGain(uint8_t v) {
    const int s = v;
    return s < 50 ? s * (1.0f / 50.0f) : 1.0f + (s - 50) * (0.6f / 50.0f);
}

void frame(uint32_t tMs, int, int, const uint8_t p[BG_ANIM_PARAMS]) {
    const uint32_t gen = themeGen();
    // Peak 64..154 above ground, exactly Math.round(glow * 0.9).
    const int peak = 64 + (static_cast<int>(p[2]) * 9 + 5) / 10;
    // The palette holds the theme ramp read through the contrast gain, so it
    // is stale when the theme, Contrast or Glow moves (Glow sets the midpoint
    // the gain turns about). buildThemeRamp(out, 256) is themeRGB(i) packed,
    // so at gain 256 this loop writes exactly what it used to write.
    bool ditherStale = false;
    if (!paletteValid || gen != lastThemeGen || lastContrast != p[6] || lastGlowPal != peak) {
        const int gQ8 = contrastQ8(p[6]);
        const int mid = GROUND + peak / 2;
        for (int i = 0; i < 256; i++) {
            uint8_t c[3];
            themeRGB(mid + ((i - mid) * gQ8) / 256, c);
            palette[i] = rgb565(c[0], c[1], c[2]);
        }
        lastThemeGen = gen;
        lastContrast = p[6];
        lastGlowPal = peak;
        paletteValid = true;
        ditherStale = true; // ditherAmp() reads the palette this just rebuilt
    }
    if (ditherStale || lastGrain != p[7]) {
        const float ampQ4 = ditherAmp(palette, 256) * grainGain(p[7]) * 16.0f / 31.5f;
        for (int k = 0; k < 64; k++) {
            // The page's bayerOffsets(..., 16) uses lround, including ties
            // away from zero. Dither follows absolute screen x/y, not scroll.
            dith[k] = static_cast<int16_t>(lroundf((static_cast<float>(BAYER8[k]) - 31.5f) * ampQ4));
        }
        lastGrain = p[7];
    }
    if (lastArc != p[1] || lastGlow != p[2] || lastSoft != p[5]) {
        const float hw = 16.0f + p[1] * 0.18f;
        const float angleScale = 3.14159265358979323846f / hw;
        // Sharpness bends the raised-cosine ring profile toward smoothstep
        // (100), which darkens the outer rim and brightens the core, or as far
        // the other way (0), which flattens the ribbon into a broad even
        // wash. The blend stays inside 0..1 and monotonic: it mixes s with
        // 3s^2-2s^3, and the far end 2s - (3s^2 - 2s^3) has derivative
        // 2 - 6s + 6s^2, whose discriminant is negative, so it never turns
        // back. Both ends of the profile are fixed points, so the arc's peak
        // and its width in pixels do not move and the tile's value range is
        // the one proved below. Exactly zero at the default, and the branch
        // keeps the arithmetic identical there rather than resting on
        // x + 0.0f * y.
        const float kSoft = (static_cast<int>(p[5]) - 50) * (1.0f / 50.0f);
        for (int o = 0; o < 2; o++) {
            uint16_t *tl = tiles + o * TILE * TILE;
            for (int y = 0; y < TILE; y++) {
                for (int x = 0; x < TILE; x++) {
                    float v = 0.0f;
                    for (int a = 0; a < 2; a++) {
                        const int cx = (o == a) ? 0 : TILE;
                        const int cy = a * TILE;
                        const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
                        const float d = fabsf(sqrtf(dx * dx + dy * dy) - HALF);
                        if (d < hw) {
                            // peak * 0.5f * u and peak * (0.5f * u) are the
                            // same single rounding: halving is exact.
                            float s = 0.5f * (1.0f + cosf(d * angleScale));
                            if (kSoft != 0.0f) s += kSoft * (s * s * (3.0f - 2.0f * s) - s);
                            const float c = peak * s;
                            if (c > v) v = c;
                        }
                    }
                    tl[y * TILE + x] = static_cast<uint16_t>(lroundf((GROUND + v) * 16.0f));
                }
            }
        }
        lastArc = p[1];
        lastGlow = p[2];
        lastSoft = p[5];
    }
    if (lastBias != p[4]) {
        // Tile bias: at the default every cell keeps the low bit of its
        // seeded byte, which is the maze. Below 50 a growing share of the
        // cells is forced to the first orientation and above 50 to the
        // second, until at either end the whole field carries one tile and
        // the arcs line up into a regular diagonal weave. The threshold is
        // exactly 0 at 50, and bits 1..7 of the same byte decide which cells
        // are forced, so the mix is a hash of the cell and not a stripe.
        const int kb = static_cast<int>(p[4]) - 50;
        const int thr = (kb < 0 ? -kb : kb) * 128 / 50;
        const uint8_t forced = kb > 0 ? 1u : 0u;
        for (int i = 0; i < 256; i++) {
            const int r = orient[i];
            obit[i] = (r >> 1) < thr ? forced : static_cast<uint8_t>(r & 1);
        }
        lastBias = p[4];
    }
    // The page rebuilds on every parameter change; the tiles, the palette,
    // the dither and the orientation table each have their own key, so a
    // slider move only pays for what it changes. Time still comes directly
    // from tMs, with no accumulated drift.
    // Floating point stays in frame(). Single precision can round a Q4 tile
    // sample differently from JS doubles; after days of uptime, the float
    // time product can also round the scroll by several pixels. Host and
    // device use the same float scheme and the same 18/12 px/s base rates.
    //
    // Drift angle turns the 18/12 px/s vector by up to 2.8 radians either
    // way, so the field can slide in any direction and the two ends of the
    // slider are 160 degrees apart rather than the same direction twice. At
    // the default the angle is exactly 0.0f, so cosf gives 1.0f and sinf
    // 0.0f, the two components are exactly 18.0f and 12.0f, and vx / 1000.0f
    // is the same correctly rounded quotient the old constant folded to.
    const float t = static_cast<float>(tMs) * speedMul(p[0]);
    const float ang = (static_cast<int>(p[3]) - 50) * (2.8f / 50.0f);
    const float ca = cosf(ang), sa = sinf(ang);
    const float vx = 18.0f * ca - 12.0f * sa;
    const float vy = 18.0f * sa + 12.0f * ca;
    // floorf, not the old truncating cast, so a backwards drift steps the
    // same way a forwards one does. Both agree for t >= 0, which is the only
    // case the default reaches. The reachable magnitude is under 7e8 pixels
    // (tMs 4.3e9 x speed 6.7 x 0.0217 px/ms), well inside the bias.
    scrollX = static_cast<uint32_t>(static_cast<int64_t>(floorf(t * (vx / 1000.0f))) +
                                    static_cast<int64_t>(SCROLL_BIAS));
    scrollY = static_cast<uint32_t>(static_cast<int64_t>(floorf(t * (vy / 1000.0f))) +
                                    static_cast<int64_t>(SCROLL_BIAS));
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            // scan[j] eventually lands at screen x = j - subX. TILE is a
            // multiple of eight, so every tile copy shares this rotation.
            dithScan[y * 8 + x] = dith[y * 8 + ((static_cast<uint32_t>(x) - scrollX) & 7u)];
        }
    }
}

GM_ANIM_IRAM void bandRef(uint16_t *__restrict dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const uint32_t cellX0 = scrollX / TILE;
    const int subX = scrollX % TILE;
    for (int row = 0; row < rows; row++) {
        const int y = y0 + row;
        const uint32_t yy = static_cast<uint32_t>(y) + scrollY;
        const int ty = yy % TILE;
        const uint32_t cellY = yy / TILE;
        for (int c = 0; c < CELLS; c++) {
            // The page's spatial hash strides are seven per cell in x and
            // thirteen per cell in y, wrapped over the 256 seeded bytes.
            const int which = obit[((cellX0 + c) * 7u + cellY * 13u) & 255u] & 1;
            memcpy(scan + c * TILE, tiles + which * TILE * TILE + ty * TILE, TILE * sizeof(uint16_t));
        }
        const int16_t *off = dith + (y & 7) * 8;
        uint16_t *out = dst + static_cast<size_t>(row) * w;
        for (int x = 0; x < w; x++) {
            int idx = (scan[x + subX] + off[x & 7]) >> 4;
            if (idx < 0) idx = 0;
            else if (idx > 255) idx = 255;
            out[x] = palette[idx];
        }
    }
}

#if GM_BGANIM_TRUCHET_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
static_assert(TILE == 128, "truchetScanAsm uses 8 blocks and a 32768-byte orientation stride");
// GCC 14.2, xtensa-asm14.sh's firmware flags, first portable compile:
// bandRef's loop is extui/addx2/l16ui/l16si/addi/add/srai/min/movi/max/
// extui/addx2/l16ui/addi/s16i/addi, 16 instructions per pixel, already
// closed by LOOP and scheduled without an immediate load-use dependency.
// Its schedule is the scalar baseline. The edge here is fusing the dither
// add with the row copies, eight lanes at once, before the scalar gathers.
// This reuses the page's scan buffer, with no extra decode/scratch pass.
//
// Every Q4 tile value is 416..2880 (ground 26, maximum peak 154), and no
// parameter widens that: Sharpness fixes both ends of the ring profile and
// Contrast acts on the palette's contents, not on the index. The shared
// ditherAmp cap is 16 indices and Grain's cap is 1.6x, so offsets are
// -410..410 and sums are 6..3290. Neither S16 saturation nor the page's
// 0..255 index clamp can fire. The gather may therefore extract bits 4..11
// directly.
//
// truchetScanAsm receives the orientation-zero tile's absolute ty row.
// Orientation one is 32,768 bytes later. Each 128-pixel copy is eight
// iterations of a 16-pixel loop: two loads, two adds and two stores.
// Alternating q0/q1 separates each load from its add by an independent
// instruction. All ee.vld/vst spans are aligned by construction: tiles
// are aligned from their PSRAM owner, ty strides 256 B, and scan and
// each 16 B dither row come from allocHot. No access uses subX here.
//
// GCC never allocates q registers, so q0/q1/q7 need no compiler clobber
// syntax. This kernel never writes CPENABLE; FreeRTOS owns lazy CP3 state.
// Only the inner copy uses hardware LOOP, the cell loop has a scalar
// back edge. No nested hardware loop and no call inside either asm block.
GM_ANIM_IRAM __attribute__((noinline)) void truchetScanAsm(uint16_t *out, const uint16_t *tileRow,
                                                         const uint8_t *orientation, const int16_t *offset,
                                                         uint32_t hash, int cells) {
    const uint16_t *src;
    uint32_t v, blocks;
    asm volatile("ee.vld.128.ip q7, %[off], 0\n"
                 "beqz    %[cells], 3f\n"
                 "2:\n"
                 "extui   %[v], %[hash], 0, 8\n"
                 "add     %[v], %[v], %[orient]\n"
                 "l8ui    %[v], %[v], 0\n"
                 "addi    %[hash], %[hash], 7\n"
                 "extui   %[v], %[v], 0, 1\n"
                 "slli    %[v], %[v], 15\n"
                 "add     %[src], %[tile], %[v]\n"
                 "movi    %[blocks], 8\n"
                 "loop    %[blocks], 1f\n"
                 "ee.vld.128.ip q0, %[src], 16\n"
                 "ee.vld.128.ip q1, %[src], 16\n"
                 "ee.vadds.s16 q0, q0, q7\n"
                 "ee.vadds.s16 q1, q1, q7\n"
                 "ee.vst.128.ip q0, %[out], 16\n"
                 "ee.vst.128.ip q1, %[out], 16\n"
                 "1:\n"
                 "addi    %[cells], %[cells], -1\n"
                 "bnez    %[cells], 2b\n"
                 "3:\n"
                 : [out] "+&r"(out), [hash] "+&r"(hash), [cells] "+&r"(cells),
                   [src] "=&r"(src), [v] "=&r"(v), [blocks] "=&r"(blocks)
                 : [tile] "r"(tileRow), [orient] "r"(orientation), [off] "r"(offset)
                 : "memory");
}

// A 13-instruction pixel-pair gather, 6.5 instructions per output pixel.
// Two l16ui inputs tolerate any halfword-aligned scan + subX, including
// odd subX. Only out must be 4-byte aligned, the BgAnim.h precondition;
// PIE never touches the caller's destination. Each load has an independent
// instruction before use, and one s32i writes the little-endian pair.
// The 6-instruction PIE loop above costs 0.375 instructions per assembled
// pixel, or 0.5 per displayed pixel at 480 wide with all five cells copied.
// These are instruction counts and a stall-free dependency schedule, not
// measured device cycles: PSRAM streaming and per-row setup still cost time.
// tools/qemubench/tests/anim_truchet executes verbatim copies of both kernels
// and the original GCC loop against an independent C reference. Device
// parity and production timing remain the final acceptance rung.
GM_ANIM_IRAM __attribute__((noinline)) void truchetGatherAsm(uint16_t *out, const uint16_t *src,
                                                           const uint16_t *pal, int pairs) {
    uint32_t p0, p1;
    asm volatile("loopnez %[pairs], 1f\n"
                 "l16ui   %[p0], %[src], 0\n"
                 "l16ui   %[p1], %[src], 2\n"
                 "extui   %[p0], %[p0], 4, 8\n"
                 "extui   %[p1], %[p1], 4, 8\n"
                 "addx2   %[p0], %[p0], %[pal]\n"
                 "addx2   %[p1], %[p1], %[pal]\n"
                 "l16ui   %[p0], %[p0], 0\n"
                 "l16ui   %[p1], %[p1], 0\n"
                 "addi    %[src], %[src], 4\n"
                 "slli    %[p1], %[p1], 16\n"
                 "or      %[p0], %[p0], %[p1]\n"
                 "s32i    %[p0], %[out], 0\n"
                 "addi    %[out], %[out], 4\n"
                 "1:\n"
                 : [out] "+&r"(out), [src] "+&r"(src), [p0] "=&r"(p0), [p1] "=&r"(p1)
                 : [pal] "r"(pal), [pairs] "r"(pairs)
                 : "memory");
}

GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const uint32_t cellXHash = (scrollX / TILE) * 7u;
    const int subX = scrollX % TILE;
    for (int row = 0; row < rows; row++) {
        // All state comes from absolute y, even when interlace skips rows
        // or band() is called with a different number of neighbouring rows.
        const int y = y0 + row;
        const uint32_t yy = static_cast<uint32_t>(y) + scrollY;
        const uint32_t hash = cellXHash + (yy / TILE) * 13u;
        truchetScanAsm(scan, tiles + (yy % TILE) * TILE, obit, dithScan + (y & 7) * 8, hash, CELLS);
        uint16_t *out = dst + static_cast<size_t>(row) * w;
        truchetGatherAsm(out, scan + subX, palette, w / 2);
        // The 233-wide half-resolution path calls one aligned row at a time.
        if (w & 1) out[w - 1] = palette[scan[subX + w - 1] >> 4];
    }
}
#else
// Host and explicitly disabled builds execute the page's portable reference.
GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
    bandRef(dst, y0, rows, w, tMs, p);
}
#endif

void release() {
    releaseTable(dithScan, 64 * sizeof(int16_t));
    releaseTable(dith, 64 * sizeof(int16_t));
    releaseTable(obit, 256);
    releaseTable(orient, 256);
    releaseTable(palette, 256 * sizeof(uint16_t));
    releaseTable(scan, SCAN * sizeof(uint16_t));
    releaseTable(tileOwner, TILE_ALLOC);
    tiles = nullptr;
    lastThemeGen = 0;
    paletteValid = false;
    lastArc = lastGlow = lastSoft = -1;
    lastContrast = lastGlowPal = lastGrain = lastBias = -1;
    scrollX = scrollY = 0;
}

} // namespace

extern const BgAnimation bg_anim_truchet;
const BgAnimation bg_anim_truchet = {
    "truchet",
    "Truchet",
    {{"speed", "Speed", 50},
     {"arc", "Arc width", 50},
     {"glow", "Glow", 55},
     {"drift", "Drift angle", 50},
     {"bias", "Tile bias", 50},
     {"sharp", "Sharpness", 50},
     {"contrast", "Contrast", 50},
     {"grain", "Grain", 50}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
