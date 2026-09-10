#ifndef GAGGIMATE_SIM

// "Barrel": wide diagonal bands climbing around a shaded vertical cylinder.
// This is entry 'barrel' in tools/animbench/web/anim_bench.html, including
// its wider, approved palette window. The arcsine maps columns onto the
// cylinder; cosine shading and a smoothstep fade hide its edges. At the
// default parameters the bands climb at 16 pixels/second at Speed 50,
// repeating in 32 seconds.
//
// The page's integer pixel scheme is preserved without resampling:
//   v = (angle[x] * bandK + y * rowStep + phase) & 131071;
//   fold = min(v, 131072 - v);
//   height = hBase + ((3 * fold) >> 3);   // hBase and the shade scale are Band depth
//   index = 22 + ((shade[x] * height) >> 16) + Bayer8[y&7][x&7].
// At the defaults rowStep is 256 and hBase 8738, and a full-shade column
// spans indices 56..151 before dither: hBase supplies 34 of those at the
// trough and the 3/8 slope adds another 95 at the crest. The page's fixed
// 1.6-index dither is retained, even when the theme changes; it is not the
// adaptive ditherAmp() rule.
//
// The page scales the theme ramp by its own Cylinder shade parameter, and
// this file keeps that: buildThemeRamp(180 + round(shade * 76 / 100)). The
// parameter therefore does two things at once, exactly as the approved page
// does, rather than only shading the cylinder body.
//
// angle is the rounded Q8 angle, 128 + 256*asin(z)/pi, times 256. col packs
// phase in bits 0..16 and shade in bits 17..26, one hot read per pixel. Its
// angle source is read only when the geometry or Bands changes. No per-row
// table is needed: each row starts from its absolute y and this frame's
// phase, including single-row interlace calls and 233-pixel rows.
//
// Eight parameters, and the five added by gm-3vj.42 all act in frame() or
// in the geometry rebuild it calls. band(), bandRef() and barrelRowAsm are
// the code they were: the kernel reads hBase out of its broadcast vector
// (vecs[8..11], written by frame() rather than by init()), rowStep is
// per-row setup in plain C, and barrel width, edge fade and light angle
// only ever change what the col table holds. Nothing touches the kernel.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

#ifndef GM_BGANIM_BARREL_ASM
#define GM_BGANIM_BARREL_ASM 1
#endif

namespace {
using namespace bganim;

constexpr uint32_t PERIOD = 131072; // 17 phase bits, 512 rows at ROW_STEP_DEF
constexpr uint32_t HALF = PERIOD >> 1;
constexpr int H_BASE_DEF = 8738;              // Band depth 50
constexpr int FOLD_SPAN = 24576;              // the fold's whole contribution to height
constexpr int CREST = H_BASE_DEF + FOLD_SPAN; // 33314, the height Band depth pins the crest to
constexpr uint32_t SHADE_MASK = 1023u << 17;  // shade reaches 346, so nine bits, not eight
constexpr int IDX_LO = 22;
constexpr int ROW_STEP_DEF = 256; // phase units per pixel of vertical travel, Band tilt 50
constexpr int CLIMB_PX_S = 16;
constexpr int EDGE_PX_DEF = 24; // Edge fade 50
constexpr int EDGE_PX_MIN = 2;  // Edge fade 0, a nearly hard silhouette edge

// Kernel geometry: the vector unit takes four columns per group and the row
// loop takes two groups, so the dither row (eight entries) is read as two
// aligned vectors and the eight palette results pack into four 32-bit
// stores.
constexpr int KERNEL_PX = 8;

uint32_t *angle = nullptr;   // cylinder geometry, frame-only reads, PSRAM
uint32_t *col = nullptr;     // packed phase and shade, hot
uint16_t *palette = nullptr; // 256-entry theme ramp, hot
int32_t *dither = nullptr;   // 64 Bayer offsets with IDX_LO folded in, hot
uint32_t *vecs = nullptr;    // four broadcast vectors then the index scratch, hot
int allocW = 0;
int lastBands = -1;
int lastShade = -1;
int lastGeom = -1;  // Barrel width, Edge fade, Light angle and Band depth, packed
int lastDepth = -1; // the height ramp base the vector currently holds
uint32_t lastThemeGen = 0xFFFFFFFFu;
uint32_t phase = 0;
int rowStep = ROW_STEP_DEF; // Band tilt, read by band() and bandRef() only
int hBase = H_BASE_DEF;     // Band depth, mirrored into vecs[8..11]

// ---- parameter maps ------------------------------------------------------
// Every one of these returns the value this file used to hard-code when its
// slider is at 50, which is why the defaults render the old picture bit for
// bit. The four float maps are written so the arithmetic is exact there:
// 50/100.0f is 0.5f, 50*44.0f/100.0f is 22.0f, CREST over CREST is 1.0f,
// and (50-50) times anything is a true zero, so cosf and sinf of it are
// exactly 1.0f and 0.0f.

// Band tilt: phase units per pixel of vertical travel. 0 at slider 0, where
// the bands stand vertical and slide sideways, ROW_STEP_DEF (256) at 50, and
// twice that at 100.
BGANIM_INLINE int tiltRowStep(uint8_t v) { return (static_cast<int>(v) * (ROW_STEP_DEF * 2) + 50) / 100; }

// Band depth: how far a band's trough falls below its crest. The height
// ramp's base runs 17476 at slider 0, H_BASE_DEF (8738) at 50 and 0 at 100,
// and the shade column is scaled by CREST / (base + FOLD_SPAN) so the crest
// lands on the same palette index whatever the base is. Moving the base
// alone would only slide the whole cylinder up and down the ramp, which is
// a brightness control, and Cylinder shade is already that; pinning the
// crest is what makes this one a depth control. Swept over all 101 sliders,
// the crest stays at index 129 and the trough runs 53 down to 0.
BGANIM_INLINE int depthBase(uint8_t v) { return (H_BASE_DEF * 2) - (static_cast<int>(v) * (H_BASE_DEF * 2) + 50) / 100; }
// Exactly 1.0f at Band depth 50, where base + FOLD_SPAN is CREST itself.
BGANIM_INLINE float depthShadeScale(int base) { return static_cast<float>(CREST) / static_cast<float>(base + FOLD_SPAN); }

// Barrel width: the cylinder's radius against the panel's half width. 0.5 at
// slider 0, where the barrel fills half the panel and the rest is unlit
// ground, exactly 1.0f at 50, 1.5f at 100, where only the middle two thirds
// of the cylinder is on screen and the shading is much flatter.
BGANIM_INLINE float widthScale(uint8_t v) { return 0.5f + static_cast<float>(v) / 100.0f; }

// Edge fade: the smoothstep's width in pixels at each side of the panel.
// EDGE_PX_MIN at slider 0, exactly EDGE_PX_DEF (24.0f) at 50, and 46 at 100.
BGANIM_INLINE float edgeWidth(uint8_t v) {
    return EDGE_PX_MIN + static_cast<float>(v) * static_cast<float>((EDGE_PX_DEF - EDGE_PX_MIN) * 2) / 100.0f;
}

// Light angle: radians the highlight sits off the cylinder's centre line.
// -0.6 at slider 0 (lit from the left), exactly 0.0f at 50, +0.6 at 100.
BGANIM_INLINE float lightAngle(uint8_t v) { return static_cast<float>(static_cast<int>(v) - 50) * (0.6f / 50.0f); }

// vecs is one 96-byte table: four broadcast vectors (phase mask, period,
// height base, row phase) and then the eight-entry index scratch the kernel
// stages its results in. They share an allocation because the kernel's four
// constant loads leave its pointer exactly at the scratch, so the scratch
// costs no argument, no second pointer and no stack slot. The scratch is in
// the slab rather than on the stack because the render task's stack is in
// PSRAM, and a store-then-load round trip through PSRAM would cost more
// than the whole group it stages.
//
// At 480 pixels: col 1,920 B, palette 512 B, dither 256 B, vecs 96 B, all
// allocHot: 2,784 of the 9,216 B slab. angle is 1,920 B via alloc(), PSRAM.
// At other widths col is rounded up by allocHot to 16 bytes. There are no
// static tables and no per-frame allocations.
//
// dither holds int32 rather than int16 so the kernel can add it to the
// multiply result in 32-bit lanes. The multiply leaves each result in the
// low half of a 32-bit lane with the high half zero, and the offsets are
// small and positive, so a 32-bit add reaches the right half with no
// carry, and no separate 16-bit layout of the same table is needed.
constexpr int VEC_WORDS = 24;  // 4 vectors of 4 words, then 8 scratch words
constexpr int VEC_SCRATCH = 16; // word offset of the index scratch
void release();

// The four geometry parameters as one comparable value. Each is 0..100, so
// the pack is exact and the top byte stays clear of the sign bit.
BGANIM_INLINE int geomKey(uint8_t width, uint8_t edge, uint8_t light, uint8_t depth) {
    return width | (edge << 8) | (light << 16) | (depth << 24);
}

// Rebuilds angle[] and col[]'s shade field for one set of geometry
// parameters. Called by init() with the defaults, and by frame() whenever
// Barrel width, Edge fade, Light angle or Band depth moves; the caller then
// rebuilds col[]'s phase field, because this writes the whole word. Band
// depth is here rather than in a block of its own because its crest pinning
// scales this shade column, and the shade column is what this builds.
//
// The light angle is the only new term in the arithmetic. The surface angle
// at column x is theta = asin(z), so the old cosine shading is cos(theta),
// and moving the light by a means cos(theta - a). Expanded through the
// angle sum that is cosA * cos(theta) + sinA * sin(theta), which is
// cosA * sqrt(1 - z*z) + sinA * z: the same square root the old line took,
// with one multiply and one add around it. At a == 0 the multipliers are
// exactly 1.0f and 0.0f, so the result is the square root unchanged, down
// to the sign of the zero it adds.
void buildGeometry(int w, uint8_t pWidth, uint8_t pEdge, uint8_t pLight, uint8_t pDepth) {
    const float half = (w - 1) * 0.5f;
    const float invRadius = 2.0f / (static_cast<float>(w) * widthScale(pWidth));
    const float edgePx = edgeWidth(pEdge);
    const float lightA = lightAngle(pLight);
    const float cosA = cosf(lightA);
    const float sinA = sinf(lightA);
    const float shadeScale = depthShadeScale(depthBase(pDepth));
    for (int x = 0; x < w; x++) {
        float z = (x - half) * invRadius;
        z = z < -1.0f ? -1.0f : (z > 1.0f ? 1.0f : z);
        const float a = 128.0f + 256.0f * asinf(z) / 3.14159265358979323846f;
        angle[x] = static_cast<uint32_t>(lroundf(a * 256.0f));
        const float body2 = 1.0f - z * z;
        float body = cosA * sqrtf(body2 > 0.0f ? body2 : 0.0f) + sinA * z;
        if (body < 0.0f) {
            body = 0.0f; // the far side of the cylinder, turned away from the light
        }
        const float e = static_cast<float>(x < w - 1 - x ? x : w - 1 - x) / edgePx;
        const float fade = e >= 1.0f ? 1.0f : e * e * (3.0f - 2.0f * e);
        col[x] = static_cast<uint32_t>(lroundf(255.0f * body * fade * shadeScale)) << 17;
    }
}

bool init(int w, int) {
    if (allocW == w && col != nullptr) {
        return true;
    }
    release();
    if (w <= 0) {
        return false;
    }
    allocW = w;
    angle = static_cast<uint32_t *>(alloc(static_cast<size_t>(w) * sizeof(uint32_t)));
    col = static_cast<uint32_t *>(allocHot(static_cast<size_t>(w) * sizeof(uint32_t)));
    palette = static_cast<uint16_t *>(allocHot(256 * sizeof(uint16_t)));
    dither = static_cast<int32_t *>(allocHot(64 * sizeof(int32_t)));
    vecs = static_cast<uint32_t *>(allocHot(VEC_WORDS * sizeof(uint32_t)));
    if (angle == nullptr || col == nullptr || palette == nullptr || dither == nullptr || vecs == nullptr) {
        release(); // a failed init leaves no live slab or heap allocation
        return false;
    }

    // Built at the defaults so a band() that somehow runs before the first
    // frame() has a whole table to read; frame() rebuilds it on its first
    // call, because lastGeom stays at its sentinel.
    buildGeometry(w, 50, 50, 50, 50);
    // lroundf has the page helper's signed, away-from-zero rounding. Its
    // offsets are -2..2, so folding in 22 gives lanes in 20..24.
    for (int k = 0; k < 64; k++) {
        dither[k] = IDX_LO + static_cast<int32_t>(lroundf((BAYER8[k] - 31.5f) * (1.6f / 31.5f)));
    }
    // Broadcast vectors, in the order the kernel loads them. The third is
    // the height ramp base, rewritten by frame() when Band depth moves, and
    // the fourth is the row phase, rewritten by band() before every row. The
    // words past them are the kernel's own scratch and need no initial value.
    for (int i = 0; i < 4; i++) {
        vecs[i] = PERIOD - 1;
        vecs[4 + i] = PERIOD;
        vecs[8 + i] = H_BASE_DEF;
        vecs[12 + i] = 0;
    }
    for (int i = VEC_SCRATCH; i < VEC_WORDS; i++) {
        vecs[i] = 0;
    }
    return true;
}

void frame(uint32_t tMs, int w, int, const uint8_t p[BG_ANIM_PARAMS]) {
    const uint32_t gen = themeGen();
    if (lastShade != p[2] || lastThemeGen != gen) {
        // This brightness belongs to the page's Cylinder shade parameter:
        // themeRamp(180 + round(shade*76/100)), a Q8 scale of 180..256.
        buildThemeRamp(palette, 180 + (static_cast<int>(p[2]) * 76 + 50) / 100);
        lastShade = p[2];
        lastThemeGen = gen;
    }
    // Barrel width, Edge fade, Light angle and Band depth share one rebuild:
    // all four write the shade column, and the width changes the angle the
    // band count is applied to. One pack, one compare, one rebuild.
    const int geom = geomKey(p[4], p[5], p[6], p[7]);
    if (lastGeom != geom) {
        buildGeometry(w, p[4], p[5], p[6], p[7]);
        lastGeom = geom;
        lastBands = -1; // buildGeometry writes the whole word, phase field included
    }
    const int bandK = 3 + (static_cast<int>(p[1]) * 7 + 50) / 100; // 3..10, default 4
    if (lastBands != bandK) {
        for (int x = 0; x < w; x++) {
            col[x] = (col[x] & SHADE_MASK) | ((angle[x] * bandK) & (PERIOD - 1));
        }
        lastBands = bandK;
    }
    // Band tilt is per-row setup in plain C, so it costs nothing to change.
    rowStep = tiltRowStep(p[3]);
    // Band depth also reaches the kernel as one of its four broadcast
    // vectors, so it costs four stores on a change and nothing per pixel.
    const int depth = depthBase(p[7]);
    if (lastDepth != depth) {
        for (int i = 0; i < 4; i++) {
            vecs[8 + i] = static_cast<uint32_t>(depth);
        }
        hBase = depth;
        lastDepth = depth;
    }

    // Directly from tMs, like the page, never from the previous band/frame.
    // Q26 holds every bit of speedMul's float over params 0..100. Using an
    // integer product retains millisecond precision through uint32 uptime
    // instead of multiplying a many-day timestamp in float. Cancelling the
    // 4096 = CLIMB_PX_S*ROW_STEP_DEF gives denominator 1000*2^(26-12).
    // At speed 50 this is exactly round(tMs*4096/1000), period 32000 ms.
    // The rate is the default tilt's, not the current tilt's, so the pattern
    // keeps moving at every tilt, including the vertical bands at tilt 0
    // where there is no vertical travel to measure. What that means for the
    // user: at a steeper tilt the same phase rate carries the bands fewer
    // pixels up the panel, so the climb reads slower, and Speed puts it back.
    // The maximum product is below 2^61, and the cast keeps the low 32
    // bits, which is more than the 17 the mask keeps. The device's float
    // speed curve and init trig can differ from JS double rounding by a
    // last bit, but the integer pixel math and theme mapping agree exactly
    // on both targets. GCC turns the 64-bit divide into a __udivdi3 call,
    // which is a device-over-host choice: it runs once a frame, against the
    // 230,400 pixels the same frame renders, and it is what keeps the phase
    // exact for the whole 49-day range of a uint32 uptime.
    constexpr uint64_t TIME_DEN = (uint64_t(1) << 26) * 1000 / (CLIMB_PX_S * ROW_STEP_DEF);
    const uint32_t speedQ26 = static_cast<uint32_t>(speedMul(p[0]) * 67108864.0f);
    phase = static_cast<uint32_t>((static_cast<uint64_t>(tMs) * speedQ26 + TIME_DEN / 2) / TIME_DEN) &
            (PERIOD - 1);
}

// Integer-only portable spec. Band depth widened two operand domains and
// that is the one thing this change had to prove again. hBase runs 17476
// down to 0, so height is 0..42052 where it was 8738..33314, and the shade
// column is scaled by CREST / (hBase + FOLD_SPAN), which is 0.792 at Band
// depth 0 and 1.356 at 100, so shade is 0..346 where it was 0..255. Both
// stay well inside the 16 unsigned bits EE.VMUL.U16 reads its operands
// from, which is the bound that matters, and shade needs 9 bits in the
// packed word rather than 8, which is what SHADE_MASK carries.
//
// The product does not grow, because the two ends move against each other:
// swept over all 101 Band depth sliders (tools/animbench, and the same
// arithmetic in the qemubench sweep), the largest is 202 * 42052 =
// 8,494,504 at slider 0, against 255 * 33314 = 8,495,070 at the default.
// The crest index is 129 at every slider and the trough runs 53 down to 0,
// so the dithered index is 20..153 exactly as it was. The page's 0..255
// clamp is therefore still redundant at every parameter setting, which is
// what lets the kernel skip it too, and the palette is still read in range.
BGANIM_INLINE uint16_t pixelRef(uint32_t packed, uint32_t r, int32_t d) {
    const uint32_t v = (packed + r) & (PERIOD - 1);
    const uint32_t f = v <= HALF ? v : PERIOD - v;
    const uint32_t hh = static_cast<uint32_t>(hBase) + ((f * 3) >> 3);
    return palette[((packed >> 17) * hh >> 16) + d];
}

GM_ANIM_IRAM void bandRef(uint16_t *__restrict dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int row = 0; row < rows; row++) {
        const int y = y0 + row;
        const uint32_t r = (static_cast<uint32_t>(y) * static_cast<uint32_t>(rowStep) + phase) & (PERIOD - 1);
        const int32_t *d = dither + (y & 7) * 8;
        uint16_t *out = dst + static_cast<size_t>(row) * w;
        for (int x = 0; x < w; x++) {
            out[x] = pixelRef(col[x], r, d[x & 7]);
        }
    }
}

#if GM_BGANIM_BARREL_ASM
#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)

// barrelRowAsm: eight pixels per iteration, PIE for the index arithmetic and
// a hand scheduled scalar tail for the palette gather, which PIE cannot do
// (there is no vector gather).
//
// Fixed point, per pixel, all of it integer and none of it wider than the
// 32-bit lanes the fold needs:
//   v  = (col[x] + r) & 131071      17 bits, so the fold is 32-bit lane work
//   f  = min(v, 131072 - v)         the triangle, 0..65536
//   hh = hBase + ((3f) >> 3)        0..42052, which fits 16 bits unsigned
//   i  = ((col[x] >> 17) * hh) >> 16 + dither   20..153, no clamp needed
//
// Why the lanes are the width they are. The phase is 17 bits, so the add,
// the mask, the reflection and the tripling are EE.*.S32 on four lanes. The
// multiply is the one place 16-bit lanes pay: shade is at most 346 and hh at
// most 42052 (the two ends of Band depth, never together), both inside 16
// bits, so EE.VMUL.U16 with SAR 16 returns
// (shade*hh)>>16 directly. Feeding it the 32-bit lane registers unchanged
// puts each operand in the even 16-bit lane with a zero above it, so four of
// the eight products are the four wanted results and the other four are
// zero. That wastes half the multiply, and it is still cheaper than the two
// EE.VUNZIP.16 a packed eight-lane multiply would need, because the wasted
// lanes cost nothing and the unzip would cost two instructions plus a second
// dither layout.
//
// Measured on the emitted code (xtensa-asm14/AnimBarrel.S, GCC 14.2): the
// loop body is 73 instructions for eight pixels, 9.1 a pixel, against the
// 22 a pixel of GCC's own bandRef loop. Two of those 22 are a data
// dependent branch and the subtract it guards, which the vector minimum
// removes outright, so the real gap on the device should be wider than the
// instruction ratio: a mispredicted fold branch costs taken-branch cycles
// on a pattern that changes with every column.
//
// The three SAR writes a group are the price of three different shift
// distances (3, 17, 16) in one dependency chain. Hoisting each shift out
// over both groups would save three instructions per eight pixels and needs
// five live working registers; there are four, because the four broadcast
// vectors hold q0 to q3.
//
// SAR is thread context and band() runs on the SleepAnim task, never in an
// ISR, so the SSAI values here survive interrupts and task switches. The
// compiler never allocates q registers, so q0 to q7 are used without a
// clobber list. Never writes CPENABLE: FreeRTOS enables PIE lazily per task
// through the coprocessor-disabled exception, which is also how another
// task's PIE state gets saved.
//
// Alignment: every ee.vld/vst span here is 16-byte aligned by construction
// (allocHot returns 16-byte aligned tables, col advances 32 bytes a group,
// the dither row is 32 bytes and the index scratch 32), and those
// instructions mask the low four address bits silently rather than trapping,
// so band() checks the tables and the row pointer before dispatching and
// falls back to bandRef when a table has spilled to PSRAM without that
// alignment.
//
// Register budget: out, colp, dith, cvec (4, updated every iteration) + pal,
// groups (2, live) + t0..t3 (4, scratch) = 10, under the ~13 usable-AR
// ceiling ASM_BRIEF.md documents, and six arguments, so none is passed on
// the stack. cvec doubles as the index-scratch pointer: the four constant
// loads post-increment it by 64 bytes, which is exactly where the scratch
// begins, and the two index stores per iteration (+16 then -16) leave it
// there.
GM_ANIM_IRAM __attribute__((noinline)) void barrelRowAsm(uint16_t *out, const uint32_t *colp, const int32_t *dith,
                                                         const uint16_t *pal, uint32_t *cvec, int groups) {
    uint16_t *outp = out;
    const uint32_t *cp = colp;
    const int32_t *dp = dith;
    uint32_t *vp = cvec;
    int n = groups;
    int32_t t0, t1, t2, t3;
    asm volatile(
        // q0 = 131071 (phase mask), q1 = 131072, q2 = hBase, q3 = r, all
        // broadcast into four 32-bit lanes by init(), frame() and band().
        // After these four loads vp addresses the index scratch.
        "ee.vld.128.ip q0, %[vp], 16\n"
        "ee.vld.128.ip q1, %[vp], 16\n"
        "ee.vld.128.ip q2, %[vp], 16\n"
        "ee.vld.128.ip q3, %[vp], 16\n"
        "beqz %[n], 2f\n"
        "1:\n"
        // ---- group A: columns x..x+3 ----
        "ee.vld.128.ip q4, %[dp], 16\n"   // dither[0..3], dither pointer -> +16
        "ee.vld.128.ip q5, %[cp], 16\n"   // packed columns
        "ee.vadds.s32 q6, q5, q3\n"       // v = packed + r (bit 17 may carry, masked next)
        "ee.andq q6, q6, q0\n"            // v &= 131071
        "ee.vsubs.s32 q7, q1, q6\n"       // 131072 - v
        "ee.vmin.s32 q6, q6, q7\n"        // f, the triangle fold
        "ee.vadds.s32 q7, q6, q6\n"       // 2f
        "ee.vadds.s32 q6, q7, q6\n"       // 3f, at most 196608
        "ssai 3\n"
        "ee.vsr.32 q6, q6\n"              // (3f) >> 3
        "ee.vadds.s32 q6, q6, q2\n"       // hh = hBase + ((3f)>>3)
        "ssai 17\n"
        "ee.vsr.32 q5, q5\n"              // shade = packed >> 17
        "ssai 16\n"
        "ee.vmul.u16 q7, q5, q6\n"        // even 16-bit lanes: (shade*hh)>>16
        "ee.vadds.s32 q7, q7, q4\n"       // + dither, which carries IDX_LO
        "ee.vst.128.ip q7, %[vp], 16\n"   // indices[0..3], index pointer -> +16
        // ---- group B: columns x+4..x+7 ----
        "ee.vld.128.ip q4, %[dp], -16\n"  // dither[4..7], pointer back to the row base
        "ee.vld.128.ip q5, %[cp], 16\n"
        "ee.vadds.s32 q6, q5, q3\n"
        "ee.andq q6, q6, q0\n"
        "ee.vsubs.s32 q7, q1, q6\n"
        "ee.vmin.s32 q6, q6, q7\n"
        "ee.vadds.s32 q7, q6, q6\n"
        "ee.vadds.s32 q6, q7, q6\n"
        "ssai 3\n"
        "ee.vsr.32 q6, q6\n"
        "ee.vadds.s32 q6, q6, q2\n"
        "ssai 17\n"
        "ee.vsr.32 q5, q5\n"
        "ssai 16\n"
        "ee.vmul.u16 q7, q5, q6\n"
        "ee.vadds.s32 q7, q7, q4\n"
        "ee.vst.128.ip q7, %[vp], -16\n"  // indices[4..7], pointer back to the base
        // ---- gather: eight palette reads, four packed 32-bit stores ----
        // Group A's store is sixteen instructions behind its first read here,
        // which is what keeps the store-to-load turnaround off the critical
        // path; inside each pair the two loads are issued before either is
        // consumed, so no load-use interlock is left unhidden.
        "l32i %[t0], %[vp], 0\n"
        "l32i %[t1], %[vp], 4\n"
        "addx2 %[t0], %[t0], %[pal]\n"
        "addx2 %[t1], %[t1], %[pal]\n"
        "l16ui %[t0], %[t0], 0\n"
        "l16ui %[t1], %[t1], 0\n"
        "l32i %[t2], %[vp], 8\n"
        "l32i %[t3], %[vp], 12\n"
        "slli %[t1], %[t1], 16\n"
        "or %[t0], %[t0], %[t1]\n"
        "s32i %[t0], %[out], 0\n"
        "addx2 %[t2], %[t2], %[pal]\n"
        "addx2 %[t3], %[t3], %[pal]\n"
        "l16ui %[t2], %[t2], 0\n"
        "l16ui %[t3], %[t3], 0\n"
        "l32i %[t0], %[vp], 16\n"
        "l32i %[t1], %[vp], 20\n"
        "slli %[t3], %[t3], 16\n"
        "or %[t2], %[t2], %[t3]\n"
        "s32i %[t2], %[out], 4\n"
        "addx2 %[t0], %[t0], %[pal]\n"
        "addx2 %[t1], %[t1], %[pal]\n"
        "l16ui %[t0], %[t0], 0\n"
        "l16ui %[t1], %[t1], 0\n"
        "l32i %[t2], %[vp], 24\n"
        "l32i %[t3], %[vp], 28\n"
        "slli %[t1], %[t1], 16\n"
        "or %[t0], %[t0], %[t1]\n"
        "s32i %[t0], %[out], 8\n"
        "addx2 %[t2], %[t2], %[pal]\n"
        "addx2 %[t3], %[t3], %[pal]\n"
        "l16ui %[t2], %[t2], 0\n"
        "l16ui %[t3], %[t3], 0\n"
        "slli %[t3], %[t3], 16\n"
        "or %[t2], %[t2], %[t3]\n"
        "s32i %[t2], %[out], 12\n"
        "addi %[out], %[out], 16\n"
        "addi %[n], %[n], -1\n"
        "bnez %[n], 1b\n"
        "2:\n"
        : [out] "+r"(outp), [cp] "+r"(cp), [dp] "+r"(dp), [vp] "+r"(vp), [n] "+r"(n), [t0] "=&r"(t0),
          [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3)
        : [pal] "r"(pal)
        : "memory");
}
#else
// Portable twin of barrelRowAsm above: same name, signature and per-group
// arithmetic, so the dispatching band() below is one piece of source
// whichever branch compiles, and the host bench, the goldens, the shapes
// check and the fuzz all exercise the same structure the device runs. This
// branch compiles on a non-Xtensa build or when GM_BGANIM_NO_ASM forces the
// kernel off. tools/qemubench/tests/anim_barrel checks the kernel against
// this arithmetic under QEMU, which is the only place both run.
void barrelRowAsm(uint16_t *out, const uint32_t *colp, const int32_t *dith, const uint16_t *pal, uint32_t *cvec,
                  int groups) {
    const uint32_t mask = cvec[0];
    const uint32_t period = cvec[4];
    const uint32_t hbase = cvec[8];
    const uint32_t r = cvec[12];
    int32_t *idx = reinterpret_cast<int32_t *>(cvec + VEC_SCRATCH);
    for (int g = 0; g < groups; g++) {
        for (int half = 0; half < 2; half++) {
            for (int lane = 0; lane < 4; lane++) {
                const int k = half * 4 + lane;
                const uint32_t packed = colp[g * KERNEL_PX + k];
                const uint32_t v = (packed + r) & mask;
                const uint32_t p = period - v;
                const uint32_t f = v < p ? v : p;
                const uint32_t hh = hbase + ((f * 3) >> 3);
                idx[k] = static_cast<int32_t>((packed >> 17) * hh >> 16) + dith[k];
            }
        }
        for (int k = 0; k < KERNEL_PX; k++) {
            out[g * KERNEL_PX + k] = pal[idx[k]];
        }
    }
}
#endif // __XTENSA__ && !GM_BGANIM_NO_ASM

// Kernel-dispatching band(): the same per-row setup as bandRef(), with the
// pixel loop handed to barrelRowAsm in groups of eight and bandRef()'s own
// scalar body finishing the row's remainder (zero pixels at this panel's 480
// and 240 widths).
//
// Two preconditions are checked rather than assumed. The vector spans must
// be 16-byte aligned, which allocHot guarantees but its PSRAM fall-back does
// not, and ee.vld/vst mask the low four address bits silently instead of
// trapping, so a spilled table would corrupt its neighbours rather than
// fault. The row's output must be 4-byte aligned for the packed stores,
// which BgAnim.h's contract (gm-bzu.21) already promises; the check costs
// one test a row and makes the kernel safe to read on its own terms. Either
// way the fall-back is bandRef, which is the same pixels.
GM_ANIM_IRAM void band(uint16_t *__restrict dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
    const uintptr_t tableBits =
        reinterpret_cast<uintptr_t>(col) | reinterpret_cast<uintptr_t>(dither) | reinterpret_cast<uintptr_t>(vecs);
    const int groups = w / KERNEL_PX;
    if ((tableBits & 15u) != 0 || groups == 0) {
        bandRef(dst, y0, rows, w, tMs, p);
        return;
    }
    for (int row = 0; row < rows; row++) {
        const int y = y0 + row;
        const uint32_t r = (static_cast<uint32_t>(y) * static_cast<uint32_t>(rowStep) + phase) & (PERIOD - 1);
        const int32_t *d = dither + (y & 7) * 8;
        uint16_t *out = dst + static_cast<size_t>(row) * w;
        if ((reinterpret_cast<uintptr_t>(out) & 3u) != 0) {
            for (int x = 0; x < w; x++) {
                out[x] = pixelRef(col[x], r, d[x & 7]);
            }
            continue;
        }
        for (int i = 0; i < 4; i++) {
            vecs[12 + i] = r;
        }
        barrelRowAsm(out, col, d, palette, vecs, groups);
        for (int x = groups * KERNEL_PX; x < w; x++) {
            out[x] = pixelRef(col[x], r, d[x & 7]);
        }
    }
}
#else
// Flag off: band() is bandRef() byte for byte, so the host golden comparison
// exercises the same code either way.
void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
    bandRef(dst, y0, rows, w, tMs, p);
}
#endif // GM_BGANIM_BARREL_ASM

void release() {
    releaseTable(angle, static_cast<size_t>(allocW) * sizeof(uint32_t));
    releaseTable(col, static_cast<size_t>(allocW) * sizeof(uint32_t));
    releaseTable(palette, 256 * sizeof(uint16_t));
    releaseTable(dither, 64 * sizeof(int32_t));
    releaseTable(vecs, VEC_WORDS * sizeof(uint32_t));
    allocW = 0;
    lastBands = -1;
    lastShade = -1;
    lastGeom = -1;
    lastDepth = -1;
    lastThemeGen = 0xFFFFFFFFu;
    phase = 0;
    rowStep = ROW_STEP_DEF;
    hBase = H_BASE_DEF;
}

} // namespace

extern const BgAnimation bg_anim_barrel;
const BgAnimation bg_anim_barrel = {
    "barrel",
    "Barrel",
    {{"speed", "Speed", 50},
     {"bands", "Bands", 14},
     {"shade", "Cylinder shade", 62},
     {"tilt", "Band tilt", 50},
     {"width", "Barrel width", 50},
     {"edge", "Edge fade", 50},
     {"light", "Light angle", 50},
     {"depth", "Band depth", 50}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
