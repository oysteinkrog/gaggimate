#ifndef GAGGIMATE_SIM

// "Truchet": broad quarter-circle paths on a dark ground, drifting diagonally.
// This is entry 30, id 'truchet', in tools/animbench/web/anim_bench.html:
// two 128x128 tiles, a seeded orientation hash and five tile-row copies per
// scanline. The approved entry's deviations are part of the design: radius
// 64, half width 16..34 pixels, and scroll 18/12 pixels per second at speed 50.
// Coordinates increase as time advances, so the visible paths move up-left,
// unless Drift angle turns the scroll vector somewhere else. The scroll is
// carried in sixteenths of a pixel (gm-kh2s): the whole part picks tile rows
// and columns, and the fraction weights a bilinear read between each row
// and the row below, then each column and the next, so every frame moves.
// With a whole pixel scroll the field advanced 0.58 and 0.38 px per 66 ms
// step at Speed 50 and 28 percent of steps repeated the previous picture.
// The blend is applied to the two tiles once per frame (blend, below), so
// the band is the same copy and gather it always was; a first version blent
// per row inside band() and doubled the band cost on the host.
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
// The blend kernel reads one vector past the row it finishes, so the tile
// store carries 16 B of read slack after the last row, then 15 B to align.
constexpr size_t TILE_ALLOC = TILE_BYTES + 16 + 15;
constexpr size_t BLEND_ALLOC = TILE_BYTES + 15;

uint8_t *tileOwner = nullptr; // PSRAM allocation owner; tiles below is an alias
uint16_t *tiles = nullptr;    // two orientations, 32,768 B each, row stride 256 B
// The tiles blended for this frame's scroll fraction (gm-kh2s): the same
// layout as tiles, rebuilt every frame. Rows 0..126 of each orientation are
// final except column 127, which needs the neighbouring cell's orientation
// and is patched per row in band() from seam. Row 127 needs the cell below,
// so blendLast holds it for each of the four orientation pairs.
uint8_t *blendOwner = nullptr; // PSRAM owner; blend below is an alias
uint16_t *blend = nullptr;     // 65,536 B, PSRAM
uint16_t *blendLast = nullptr; // 1,024 B, PSRAM: [(o * 2 + below) * TILE + x]
uint16_t *seam = nullptr;      // 1,024 B, slab: [(o * 2 + right) * TILE + ty], ty 0..126
uint16_t *scan = nullptr;      // 1,280 B, slab: assembled Q4 scanline
uint16_t *scanB = nullptr;     // 1,280 B, slab: the row below it, bandRef only
uint16_t *palette = nullptr;  // 512 B, slab: direct 256-entry theme ramp
uint8_t *orient = nullptr;    // 256 B, slab: full mulberry32 output bytes
uint8_t *obit = nullptr;      // 256 B, slab: the orientation bit Tile bias picked
int16_t *dith = nullptr;      // 128 B, slab: eight Bayer rows, Q4 index offsets
int16_t *dithScan = nullptr;  // 128 B, slab: same rows rotated by -subX
int16_t *zeroOff = nullptr;   // 16 B, slab: a zero dither row for the plain copies

uint32_t lastThemeGen = 0;
int lastArc = -1, lastGlow = -1, lastSoft = -1;
int lastContrast = -1, lastGlowPal = -1, lastGrain = -1, lastBias = -1;
bool paletteValid = false;
uint32_t scrollX = 0, scrollY = 0; // whole pixels, biased
uint32_t fracX = 0, fracY = 0;     // sixteenths, 0..15

// The scroll accumulators are unsigned, and Drift angle can point the scroll
// vector backwards, so both carry this bias. It is a multiple of 256 * TILE,
// which leaves the orientation hash alone (the hash takes cellX * 7 and
// cellY * 13 modulo 256, and 256 cells of stride change both by a multiple of
// 256), a multiple of TILE, which leaves subX and ty alone, and a multiple of
// 8, which leaves the dither phase alone. So it is invisible in the output.
constexpr uint32_t SCROLL_BIAS = 0x40000000u;
static_assert(SCROLL_BIAS % (256u * TILE) == 0, "SCROLL_BIAS must not move the orientation hash");

// 4,880 B of the 9,216 B animation slab, independent of render resolution.
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
    if (blendOwner == nullptr) {
        blendOwner = static_cast<uint8_t *>(alloc(BLEND_ALLOC));
        if (blendOwner == nullptr) {
            release();
            return false;
        }
        blend = reinterpret_cast<uint16_t *>((reinterpret_cast<uintptr_t>(blendOwner) + 15u) & ~uintptr_t(15));
    }
    if (blendLast == nullptr) blendLast = static_cast<uint16_t *>(alloc(4 * TILE * sizeof(uint16_t)));
    if (seam == nullptr) seam = static_cast<uint16_t *>(allocHot(4 * TILE * sizeof(uint16_t)));
    if (scan == nullptr) scan = static_cast<uint16_t *>(allocHot(SCAN * sizeof(uint16_t)));
    if (scanB == nullptr) scanB = static_cast<uint16_t *>(allocHot(SCAN * sizeof(uint16_t)));
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
    if (zeroOff == nullptr) {
        zeroOff = static_cast<int16_t *>(allocHot(8 * sizeof(int16_t)));
        if (zeroOff != nullptr) memset(zeroOff, 0, 8 * sizeof(int16_t));
    }
    if (blendLast == nullptr || seam == nullptr || scan == nullptr || scanB == nullptr || palette == nullptr ||
        orient == nullptr || obit == nullptr || dith == nullptr || dithScan == nullptr || zeroOff == nullptr) {
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

// The row's Q4 values blended in y between the row above and the row below
// (fy sixteenths), then in x between each entry and the next (fx), then the
// screen-anchored dither added. The shifts are arithmetic, the floor the
// PIE's EE.VMUL.S16 takes, so the kernel reproduces this bit for bit. A
// blend never leaves the range of its two inputs, so the 416..2880 tile
// range and the index proof the kernels rely on are untouched.
#if GM_BGANIM_TRUCHET_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
inline int lerpQ4(int a, int b, int f) { return a + (((b - a) * f) >> 4); }
#endif

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

// The bilinear blend, eight entries per iteration: out = H(V(a, b)) + dither
// where V is the y blend of rows a and b at fy sixteenths and H the x blend
// of each entry with the next at fx. Per output vector k: ly for blocks k
// and k + 1, the block k + 1 window of ly through EE.SRC.Q with SAR_BYTE 2
// (one 16-bit lane), then ly + (((ly1 - ly) * fx) >> 4) + dither.
// EE.VMUL.S16 shifts the 32-bit product right by SAR (4 here) and keeps the
// low 16 bits, which is the C reference's arithmetic shift; no product
// reaches 16 bits ((2880 - 416) * 15 >> 4 is 2310) and no sum saturates
// (3290 at most with the dither), so the saturating adds are plain adds.
//
// Iteration k loads block k + 1 of both rows before it stores block k, so
// out may equal a, and n8 iterations read entries 0..8 * n8 + 7 of both
// rows: one vector past the row, which TILE_ALLOC's slack covers for the
// last row of the second tile. Every pointer is 16-byte aligned by
// construction (aligned owners, allocHot, whole vectors), and the dither
// row is one aligned 16-byte load. fx2 and fy2 carry the fraction in both
// halves so four EE.MOVI.32.Q fill a lane vector without a table. q0..q7
// are compiler-unallocated; SAR and SAR_BYTE belong to the task context
// and CPENABLE is never written here. 13 instructions per 8 entries.
GM_ANIM_IRAM __attribute__((noinline)) void truchetLerpAsm(uint16_t *out, const uint16_t *a, const uint16_t *b,
                                                         const int16_t *dither, uint32_t fx2, uint32_t fy2,
                                                         int n8) {
    const uint16_t *aNext = a + 8;
    uint32_t sarByte = 2;
    asm volatile("ee.vld.128.ip q7, %[dither], 0\n"
                 "ee.movi.32.q q5, %[fx2], 0\n"
                 "ee.movi.32.q q5, %[fx2], 1\n"
                 "ee.movi.32.q q5, %[fx2], 2\n"
                 "ee.movi.32.q q5, %[fx2], 3\n"
                 "ee.movi.32.q q6, %[fy2], 0\n"
                 "ee.movi.32.q q6, %[fy2], 1\n"
                 "ee.movi.32.q q6, %[fy2], 2\n"
                 "ee.movi.32.q q6, %[fy2], 3\n"
                 "ssai 4\n"
                 "wur.sar_byte %[sb]\n"
                 "ee.vld.128.ip q0, %[a], 0\n" // block 0 of both rows
                 "ee.vld.128.ip q1, %[b], 16\n"
                 "ee.vsubs.s16 q1, q1, q0\n"
                 "ee.vmul.s16 q1, q1, q6\n"
                 "ee.vadds.s16 q2, q0, q1\n" // ly of block 0
                 "loopnez %[n], 1f\n"
                 "ee.vld.128.ip q0, %[an], 16\n" // block k + 1 of both rows
                 "ee.vld.128.ip q1, %[b], 16\n"
                 "ee.vsubs.s16 q1, q1, q0\n"
                 "ee.vmul.s16 q1, q1, q6\n"
                 "ee.vadds.s16 q3, q0, q1\n" // ly of block k + 1
                 "ee.src.q q4, q2, q3\n"     // ly entries 8k + 1 .. 8k + 8
                 "ee.vsubs.s16 q4, q4, q2\n"
                 "ee.vmul.s16 q4, q4, q5\n"
                 "ee.vadds.s16 q4, q4, q2\n"
                 "ee.vadds.s16 q4, q4, q7\n"
                 "ee.vst.128.ip q4, %[out], 16\n" // block k
                 "ee.orq q2, q3, q3\n"
                 "1:\n"
                 : [out] "+&r"(out), [an] "+&r"(aNext), [b] "+&r"(b)
                 : [a] "r"(a), [dither] "r"(dither), [fx2] "r"(fx2), [fy2] "r"(fy2), [sb] "r"(sarByte),
                   [n] "r"(n8)
                 : "memory");
}
#else
// Portable twins of the three kernels, same names and contracts, so the host
// bench and a build with the flag off run the same band() the device runs,
// with only the kernel bodies swapped (the arrangement AnimSilk2.cpp uses).
inline int lerpQ4(int a, int b, int f) { return a + (((b - a) * f) >> 4); }

void truchetScanAsm(uint16_t *out, const uint16_t *tileRow, const uint8_t *orientation, const int16_t *offset,
                    uint32_t hash, int cells) {
    for (int c = 0; c < cells; c++) {
        const uint16_t *src = tileRow + (orientation[(hash + 7u * c) & 255u] & 1) * TILE * TILE;
        for (int x = 0; x < TILE; x++) out[c * TILE + x] = static_cast<uint16_t>(src[x] + offset[x & 7]);
    }
}

void truchetGatherAsm(uint16_t *out, const uint16_t *src, const uint16_t *pal, int pairs) {
    for (int i = 0; i < 2 * pairs; i++) out[i] = pal[(src[i] >> 4) & 255];
}

void truchetLerpAsm(uint16_t *out, const uint16_t *a, const uint16_t *b, const int16_t *dither, uint32_t fx2,
                    uint32_t fy2, int n8) {
    const int fx = static_cast<int>(fx2 & 0xffffu), fy = static_cast<int>(fy2 & 0xffffu);
    int ly = lerpQ4(a[0], b[0], fy);
    for (int i = 0; i < 8 * n8; i++) {
        const int ly1 = lerpQ4(a[i + 1], b[i + 1], fy);
        out[i] = static_cast<uint16_t>(lerpQ4(ly, ly1, fx) + dither[i & 7]);
        ly = ly1;
    }
}
#endif

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
    // Speed calibration (gm-33fm): the tiles' half change time was 577 ms,
    // 2.1 times faster than the fleet target of 1200 ms at Speed 50. 31/64
    // lands on 1191 ms and is exact in float and double. t is the drift
    // distance along the tile field, so the whole scroll slows and the tile
    // art, the palette and the dither are untouched. At 18 and 12 px/s
    // scaled by 31/64 the field advances 0.58 and 0.38 px per 66 ms panel
    // row refresh, which a whole pixel scroll cannot show on every refresh;
    // the sixteenths below and the bilinear read in the band are what make
    // every refresh move (gm-kh2s). With the fraction carried the half
    // change time reads 1135 ms against 1117 before, so the rate stays.
    const float t = static_cast<float>(tMs) * speedMul(p[0]) * (31.0f / 64.0f);
    const float ang = (static_cast<int>(p[3]) - 50) * (2.8f / 50.0f);
    const float ca = cosf(ang), sa = sinf(ang);
    const float vx = 18.0f * ca - 12.0f * sa;
    const float vy = 18.0f * sa + 12.0f * ca;
    // floorf, not a truncating cast, so a backwards drift steps the same
    // way a forwards one does. The scroll is floored in sixteenths: the
    // multiply by 16 is exact in float, the arithmetic shift of the int64
    // floors the whole part for a negative drift too, and the low four bits
    // are the fraction. The reachable magnitude is under 7e8 pixels (tMs
    // 4.3e9 x speed 6.7 x 0.0217 px/ms), well inside the bias; past about a
    // day of uptime the float product no longer resolves a sixteenth, and
    // the fraction then steps coarser, which is the limit the whole pixel
    // version already had.
    const int64_t sx16 = static_cast<int64_t>(floorf(t * (vx / 1000.0f) * 16.0f));
    const int64_t sy16 = static_cast<int64_t>(floorf(t * (vy / 1000.0f) * 16.0f));
    scrollX = static_cast<uint32_t>((sx16 >> 4) + static_cast<int64_t>(SCROLL_BIAS));
    scrollY = static_cast<uint32_t>((sy16 >> 4) + static_cast<int64_t>(SCROLL_BIAS));
    fracX = static_cast<uint32_t>(sx16 & 15);
    fracY = static_cast<uint32_t>(sy16 & 15);
    // The blended tiles for this frame. Rows 0..126 of each orientation
    // blend with their own next row; row 127 blends with row 0 of the cell
    // below, one copy per orientation pair. Column 127 of every row is the
    // seam with the cell to the right, kept per orientation pair in seam
    // (rows 0..126; the four corners at row 127 are computed in band(), one
    // per cell every 128 rows). The kernel reads one vector past each row,
    // which TILE_ALLOC's slack covers, and its column 127 output is the
    // one seam overwrites. This is 254 rows of blend a frame against the
    // 480 rows the first version blent inside band().
    const uint32_t fx2 = fracX | (fracX << 16), fy2 = fracY | (fracY << 16);
    const int fx = static_cast<int>(fracX), fy = static_cast<int>(fracY);
    for (int o = 0; o < 2; o++) {
        const uint16_t *tl = tiles + o * TILE * TILE;
        uint16_t *bl = blend + o * TILE * TILE;
        for (int ty = 0; ty < TILE - 1; ty++) {
            truchetLerpAsm(bl + ty * TILE, tl + ty * TILE, tl + (ty + 1) * TILE, zeroOff, fx2, fy2, TILE / 8);
        }
        for (int ob = 0; ob < 2; ob++) {
            truchetLerpAsm(blendLast + (o * 2 + ob) * TILE, tl + (TILE - 1) * TILE, tiles + ob * TILE * TILE, zeroOff,
                           fx2, fy2, TILE / 8);
        }
    }
    for (int o = 0; o < 2; o++) {
        for (int o1 = 0; o1 < 2; o1++) {
            const uint16_t *ta = tiles + o * TILE * TILE + (TILE - 1);
            const uint16_t *tb = tiles + o1 * TILE * TILE;
            uint16_t *sm = seam + (o * 2 + o1) * TILE;
            for (int ty = 0; ty < TILE - 1; ty++) {
                const int lyA = lerpQ4(ta[ty * TILE], ta[(ty + 1) * TILE], fy);
                const int lyB = lerpQ4(tb[ty * TILE], tb[(ty + 1) * TILE], fy);
                sm[ty] = static_cast<uint16_t>(lerpQ4(lyA, lyB, fx));
            }
        }
    }
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            // scan[j] eventually lands at screen x = j - subX. TILE is a
            // multiple of eight, so every tile copy shares this rotation.
            dithScan[y * 8 + x] = dith[y * 8 + ((static_cast<uint32_t>(x) - scrollX) & 7u)];
        }
    }
}

// One tile row of the field for absolute row yy: five cell copies, each
// picked by the page's spatial hash (seven per cell in x, thirteen per cell
// in y, wrapped over the 256 seeded bytes).
inline void assembleRow(uint16_t *dstScan, uint32_t yy, uint32_t cellX0) {
    const int ty = yy % TILE;
    const uint32_t cellY = yy / TILE;
    for (int c = 0; c < CELLS; c++) {
        const int which = obit[((cellX0 + c) * 7u + cellY * 13u) & 255u] & 1;
        memcpy(dstScan + c * TILE, tiles + which * TILE * TILE + ty * TILE, TILE * sizeof(uint16_t));
    }
}

// The reference blends per row straight from the unblended tiles, so it
// shares nothing with the per-frame blend the band reads.
GM_ANIM_IRAM void bandRef(uint16_t *__restrict dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const uint32_t cellX0 = scrollX / TILE;
    const int subX = scrollX % TILE;
    const int fx = static_cast<int>(fracX), fy = static_cast<int>(fracY);
    for (int row = 0; row < rows; row++) {
        const int y = y0 + row;
        const uint32_t yy = static_cast<uint32_t>(y) + scrollY;
        assembleRow(scan, yy, cellX0);
        assembleRow(scanB, yy + 1u, cellX0);
        // In place: entry j reads only entries j of both rows.
        for (int j = 0; j < SCAN; j++) {
            scan[j] = static_cast<uint16_t>(lerpQ4(scan[j], scanB[j], fy));
        }
        const int16_t *off = dith + (y & 7) * 8;
        uint16_t *out = dst + static_cast<size_t>(row) * w;
        for (int x = 0; x < w; x++) {
            const int j = x + subX;
            int idx = (lerpQ4(scan[j], scan[j + 1], fx) + off[x & 7]) >> 4;
            if (idx < 0) idx = 0;
            else if (idx > 255) idx = 255;
            out[x] = palette[idx];
        }
    }
}

GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const uint32_t cellXHash = (scrollX / TILE) * 7u;
    const int subX = scrollX % TILE;
    const int fx = static_cast<int>(fracX), fy = static_cast<int>(fracY);
    for (int row = 0; row < rows; row++) {
        // All state comes from absolute y, even when interlace skips rows
        // or band() is called with a different number of neighbouring rows.
        const int y = y0 + row;
        const uint32_t yy = static_cast<uint32_t>(y) + scrollY;
        const int ty = yy % TILE;
        const uint32_t hash = cellXHash + (yy / TILE) * 13u;
        const int16_t *off = dithScan + (y & 7) * 8;
        if (ty != TILE - 1) {
            truchetScanAsm(scan, blend + ty * TILE, obit, off, hash, CELLS);
        } else {
            // The last tile row blends with row 0 of the cell below (hash
            // + 13), so it is picked per cell from the four prepared pairs.
            for (int c = 0; c < CELLS; c++) {
                const int o = obit[(hash + 7u * c) & 255u] & 1;
                const int ob = obit[(hash + 13u + 7u * c) & 255u] & 1;
                const uint16_t *src = blendLast + (o * 2 + ob) * TILE;
                uint16_t *d = scan + c * TILE;
                for (int x = 0; x < TILE; x++) d[x] = static_cast<uint16_t>(src[x] + off[x & 7]);
            }
        }
        // Column 127 of each cell blends into column 0 of the cell to its
        // right, whose orientation the copy did not know. Cell 4's seam is
        // entry 639, past the last entry the gather reads (606), so it is
        // left as copied.
        for (int c = 0; c < CELLS - 1; c++) {
            const int o = obit[(hash + 7u * c) & 255u] & 1;
            const int o1 = obit[(hash + 7u * (c + 1)) & 255u] & 1;
            int v;
            if (ty != TILE - 1) {
                v = seam[(o * 2 + o1) * TILE + ty];
            } else {
                const int ob = obit[(hash + 13u + 7u * c) & 255u] & 1;
                const int ob1 = obit[(hash + 13u + 7u * (c + 1)) & 255u] & 1;
                const int lyA = lerpQ4(tiles[o * TILE * TILE + (TILE - 1) * TILE + (TILE - 1)],
                                       tiles[ob * TILE * TILE + (TILE - 1)], fy);
                const int lyB = lerpQ4(tiles[o1 * TILE * TILE + (TILE - 1) * TILE], tiles[ob1 * TILE * TILE], fy);
                v = lerpQ4(lyA, lyB, fx);
            }
            scan[c * TILE + TILE - 1] = static_cast<uint16_t>(v + off[7]);
        }
        uint16_t *out = dst + static_cast<size_t>(row) * w;
        truchetGatherAsm(out, scan + subX, palette, w / 2);
        // The 233-wide half-resolution path calls one aligned row at a time.
        if (w & 1) out[w - 1] = palette[scan[subX + w - 1] >> 4];
    }
}

void release() {
    releaseTable(zeroOff, 8 * sizeof(int16_t));
    releaseTable(dithScan, 64 * sizeof(int16_t));
    releaseTable(dith, 64 * sizeof(int16_t));
    releaseTable(obit, 256);
    releaseTable(orient, 256);
    releaseTable(palette, 256 * sizeof(uint16_t));
    releaseTable(scanB, SCAN * sizeof(uint16_t));
    releaseTable(scan, SCAN * sizeof(uint16_t));
    releaseTable(seam, 4 * TILE * sizeof(uint16_t));
    releaseTable(blendLast, 4 * TILE * sizeof(uint16_t));
    releaseTable(blendOwner, BLEND_ALLOC);
    releaseTable(tileOwner, TILE_ALLOC);
    tiles = blend = nullptr;
    lastThemeGen = 0;
    paletteValid = false;
    lastArc = lastGlow = lastSoft = -1;
    lastContrast = lastGlowPal = lastGrain = lastBias = -1;
    scrollX = scrollY = 0;
    fracX = fracY = 0;
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
