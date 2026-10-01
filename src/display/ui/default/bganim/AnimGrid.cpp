#ifndef GAGGIMATE_SIM

// "Grid": a dim perspective wire floor without a horizon. This is entry 40
// of tools/animbench/web/anim_bench.html, including its softened shoulders,
// depth fade and fixed Bayer grain. Lines travel down at about 8 px/s at the
// far edge and 30 px/s at the near edge, with a slow leftward drift.
//
// A[y] = round(960 * 65536 / (y+h)) advances a Q16 cursor across each row.
// Vq8[y] = round(552960 * 256 / (y+h)) sets the horizontal crossings, about
// 4.5 on a 480-row panel. Both folds have a 32768-unit Q8 period. Width and
// a rounded reciprocal normalize each line to 0..255; max(vertical,
// horizontal) feeds a 256-entry smoothstep, then depth gain and the theme
// ramp. The page's actual flow period is 32768/4914 seconds (about 6.668),
// while the lateral phase repeats after 64 seconds. Its header's "64 s"
// does not describe a joint repeat. Keep the executable rates unchanged.
//
// The page builds its palette with themeRamp(190 + round(lines*66/100)),
// so the Line strength param scales the theme ramp's brightness as well as
// the line amplitude. BgAnimCommon.h's buildThemeRamp() comment says every
// caller in the tree passes 256 and warns against wiring a second control
// to it. This animation is the exception the owner approved on the page,
// the same call AnimKaleido.cpp already makes: the scale is part of the
// look that was signed off, not a user-facing brightness control, and the
// user-facing one is still setThemeTone().
//
// Five more parameters landed on 2026-09-10 (gm-3vj.43): line width, cross
// lines, grid reach, floor shade and side drift. Each one is exactly the
// constant this file used to hard-code when its slider sits at 50, so the
// default picture is the old picture bit for bit. None of them reaches the
// pixel loop. Line width and floor shade rebuild per-row table entries,
// cross lines scales the horizontal term frame() already writes each frame,
// grid reach reshapes the depth gain, and side drift scales the lateral
// phase rate. band(), bandRef() and both kernels are unchanged code.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>
#include <stddef.h>
#include <stdint.h>

// Master switch for the hand-written Xtensa kernel below. -DGM_BGANIM_GRID_ASM=0
// makes band() bandRef() byte for byte.
#ifndef GM_BGANIM_GRID_ASM
#define GM_BGANIM_GRID_ASM 1
#endif

namespace {
using namespace bganim;

constexpr int GRID_NUM = 960;
constexpr int FLOW_NUM = 552960;
constexpr int LINE_W = 3072; // Q8 half-width before the depth widening
constexpr int LINE_AMP = 82;
constexpr uint32_t U_PERIOD = 32768u << 8; // Q16 phase, one line period
constexpr int FLOW_Q8_PER_S = 4914;
constexpr int DRIFT_Q16_PER_S = 2 * 65536;
// Params reach band() as raw bytes. Every one of them is 0..100 by contract
// (the registry defs, the settings rows and tools/animbench/fuzz.cpp all
// stay inside it), and the palette-index proof above bandRef() depends on
// that, so frame() clamps rather than trusting it. Clamping costs one
// compare per frame and nothing per pixel.
constexpr int PARAM_MAX = 100;

// The page's U0, du, lw, kv and G, ampRow, bgRow, losslessly narrowed and
// split to avoid a padding byte per row. Only u0 is unsigned: mod 2^32
// addition followed by bits 8..22 is identical to JS's signed shift/mask,
// because (v >> 8) & 32767 keeps bits 8..22 of v whichever way the shift
// fills the top.
struct CursorRow {
    uint32_t u0, du;
    uint16_t lw, kv;
};
struct ToneRow {
    uint8_t g, amp, bg;
};
// A, Vq8, lwH and kh are only read while building frame state. Their
// reciprocals retain the page's rounding and full fractional precision.
struct DepthRow {
    uint32_t a, vq8;
    uint16_t lwH, kh;
};
static_assert(sizeof(CursorRow) == 12 && sizeof(ToneRow) == 3 && sizeof(DepthRow) == 12,
              "grid table budget depends on these packed field sizes");

CursorRow *cursor = nullptr;
ToneRow *tone = nullptr;
DepthRow *depth = nullptr;
uint8_t *profile = nullptr; // the page's PROF, 0..255 in one byte each
uint16_t *palette = nullptr;
int16_t *dither = nullptr;
int allocW = 0, allocH = 0;
// The tone tables cover line strength, grid reach and floor shade, so one
// key gates all three; line width has its own, because its rebuild is the
// heavier one and the two move independently.
int lastToneKey = -1;
int lastWidth = -1;
uint32_t lastThemeGen = 0;

// At 480 rows every table read per pixel or per row fits the 9,216 B hot
// slab: cursor 5,760 + tone 1,440 + profile 256 + palette 512 + dither 128
// = 8,096 B. DepthRow is 5,760 B in PSRAM, swept once per row by frame()
// only. No table is static, so none of this is BSS the web UI pays for.
void release();

bool init(int w, int h) {
    if (w <= 0 || h <= 0 || h > 480) {
        release();
        return false; // the supported panels fit this fixed slab budget
    }
    if (cursor != nullptr && allocW == w && allocH == h) {
        return true;
    }
    release();
    allocW = w;
    allocH = h;
    cursor = static_cast<CursorRow *>(allocHot(static_cast<size_t>(h) * sizeof(CursorRow)));
    tone = static_cast<ToneRow *>(allocHot(static_cast<size_t>(h) * sizeof(ToneRow)));
    profile = static_cast<uint8_t *>(allocHot(256));
    palette = static_cast<uint16_t *>(allocHot(256 * sizeof(uint16_t)));
    dither = static_cast<int16_t *>(allocHot(64 * sizeof(int16_t)));
    depth = static_cast<DepthRow *>(alloc(static_cast<size_t>(h) * sizeof(DepthRow)));
    if (!cursor || !tone || !profile || !palette || !dither || !depth) {
        release(); // includes every successful allocation before the failed one
        return false;
    }
    // round(255*u*u*(3-2*u)), u=i/255, evaluated as the exact rational
    // polynomial. Positive half-up rounding matches Math.round, without a
    // host/device libm or FPU rounding difference in the immutable table.
    for (int i = 0; i < 256; i++) {
        profile[i] = static_cast<uint8_t>((i * i * (765 - 2 * i) + 32512) / 65025);
    }
    for (int k = 0; k < 64; k++) {
        // (BAYER8-31.5)*1.4/31.5 = (2*BAYER8-63)/45. The page's
        // bayerOffsets uses lround, including away-from-zero negative ties.
        const int n = 2 * BAYER8[k] - 63;
        dither[k] = static_cast<int16_t>(n < 0 ? -((-n + 22) / 45) : (n + 22) / 45);
    }
    // Only the perspective terms are fixed at this size. Everything else a
    // row needs depends on a parameter, so frame() owns it: both sentinels
    // are -1 here (release() ran above, or this is the first init), which
    // makes the first frame() build them before any band() reads them.
    for (int y = 0; y < h; y++) {
        DepthRow &d = depth[y];
        const int den = y + h;
        d.a = (GRID_NUM * 65536 + den / 2) / den;
        d.vq8 = (FLOW_NUM * 256 + den / 2) / den;
    }
    return true;
}

// Line width. wmul is the half-width multiplier in hundredths: 40 at slider
// 0, exactly 100 at 50, 180 at 100. At 100 this is the same rational the
// original init() evaluated, numerator and denominator both scaled by 100,
// and dw = 100*(h-1) is always even, so the default table comes out entry
// for entry the table it used to be.
void buildWidth(int h, int wmul) {
    const int dh = h > 1 ? h - 1 : 1; // define the one-row degenerate panel
    const int dw = 100 * dh;
    for (int y = 0; y < h; y++) {
        CursorRow &c = cursor[y];
        DepthRow &d = depth[y];
        // lw = round(3072*(0.85+0.55*y/(h-1))*wmul/100). Keeping the exact
        // hundredths avoids a float half-tie changing the width by one.
        const int64_t num = static_cast<int64_t>(LINE_W) * (85 * dh + 55 * y) * wmul;
        c.lw = static_cast<uint16_t>((num + dw * 50) / (static_cast<int64_t>(dw) * 100));
        c.kv = static_cast<uint16_t>((255 * 65536 + c.lw / 2) / c.lw);
        // Horizontal width is 1.35x at the far edge, tapering to 1x near.
        const int64_t numH = static_cast<int64_t>(c.lw) * (135 * dh - 35 * y);
        d.lwH = static_cast<uint16_t>((numH + dw / 2) / dw);
        d.kh = static_cast<uint16_t>((255 * 65536 + d.lwH / 2) / d.lwH);
    }
}

// The palette and both per-row tone terms. lenq is the depth fade's ease
// length in hundredths (95 at grid reach 0, exactly 55 at 50, 15 at 100) and
// span is how many palette indices the floor gains from the far edge to the
// near one (0 at floor shade 0, exactly 24 at 50, 48 at 100).
void buildTone(int h, int lines, int lenq, int span) {
    // The page scales RGB channels by this Q8 brightness before 565
    // quantization. Scaling an already-quantized ramp would differ.
    buildThemeRamp(palette, static_cast<uint16_t>(190 + (lines * 66 + 50) / 100));
    const int amp = (LINE_AMP * (550 + 9 * lines) + 500) / 1000;
    const int dh = h > 1 ? h - 1 : 1;
    const float invH = 1.0f / dh;
    // 55.0f/100.0f is the same float the literal 0.55f was, both being the
    // correctly rounded value of 0.55, and the divide below is the same
    // operation on the same operands, so grid reach 50 is bit exact.
    const float lenf = static_cast<float>(lenq) / 100.0f;
    for (int y = 0; y < h; y++) {
        // Depth gain: exactly zero over the top 5% of rows, easing in to
        // full over the next lenf of the panel. The grid has to end, not
        // thin out: the rows above the ease-in are where the feather would
        // be a couple of rows wide and the crossings would bunch.
        float f = (y * invH - 0.05f) / lenf;
        f = f < 0 ? 0 : (f > 1 ? 1 : f);
        tone[y].amp = static_cast<uint8_t>(lroundf(amp * f * f * (3.0f - 2.0f * f)));
        // near-to-far ramp: the far grid sits on a slightly darker floor.
        tone[y].bg = static_cast<uint8_t>(60 + (span * y + dh / 2) / dh);
    }
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[BG_ANIM_PARAMS]) {
    const int lines = p[2] > PARAM_MAX ? PARAM_MAX : p[2];
    const int density = p[1] > PARAM_MAX ? PARAM_MAX : p[1];
    const int width = p[3] > PARAM_MAX ? PARAM_MAX : p[3];
    const int cross = p[4] > PARAM_MAX ? PARAM_MAX : p[4];
    const int reach = p[5] > PARAM_MAX ? PARAM_MAX : p[5];
    const int shade = p[6] > PARAM_MAX ? PARAM_MAX : p[6];
    const int driftAmt = p[7] > PARAM_MAX ? PARAM_MAX : p[7];
    // Line width: 0.40x at slider 0, exactly 1x at 50, 1.80x at 100. Keyed
    // on the multiplier, not the slider, so neighbouring slider values that
    // land on the same hundredths skip the rebuild.
    const int wmul = width <= 50 ? 40 + (width * 60) / 50 : 100 + ((width - 50) * 80) / 50;
    if (lastWidth != wmul) {
        buildWidth(h, wmul);
        lastWidth = wmul;
    }
    // Grid reach: the ease length, 0.95 at 0, exactly 0.55 at 50, 0.15 at
    // 100, so a higher slider brings the grid to full strength further up
    // the panel and a lower one keeps it near the viewer.
    const int lenq = reach <= 50 ? 95 - (reach * 40) / 50 : 55 - ((reach - 50) * 40) / 50;
    // Floor shade: the near-to-far lift, flat at 0, exactly 24 at 50, 48 at
    // 100. bg therefore spans 60..108 rather than the old fixed 60..84.
    const int span = (48 * shade + 50) / 100;
    const uint32_t gen = themeGen();
    const int toneKey = lines | (lenq << 8) | (span << 16);
    if (lastToneKey != toneKey || lastThemeGen != gen) {
        buildTone(h, lines, lenq, span);
        lastToneKey = toneKey;
        lastThemeGen = gen;
    }
    // Derive phases from tMs, as the page does, never from call history.
    // Q24 stores speedMul's float result before the long time product, so
    // millis() near UINT32_MAX does not throw away sub-second motion in a
    // float multiplication. JS uses double here; the float speed curve can
    // differ by a few phase units after long uptimes, but never overflows.
    // Speed calibration, gm-33fm 2026-09-12: the base rate carries a
    // deliberate factor so that Speed 50 moves this animation about as much
    // per second as every other animation at Speed 50. Grid ran 1.7 times
    // too fast, so the factor is 19/32. It is written as a binary fraction
    // on purpose: 16777216*19/32 is exact in float and in the page's double,
    // so the integer phase arithmetic below still lands on the page's value
    // rather than a unit either side of it.
    const uint32_t speedQ24 =
        static_cast<uint32_t>(lroundf(speedMul(p[0]) * 16777216.0f * 19.0f / 32.0f));
    const uint64_t scaledMs = static_cast<uint64_t>(tMs) * speedQ24;
    constexpr uint64_t TIME_DEN = 1000ull * 16777216ull;
    // Side drift scales the 2 Q16 units per second lateral rate: held still
    // at slider 0, exactly the old rate at 50, twice it at 100. dmul carries
    // that in hundredths, so 100 divides back to the original constant with
    // no rounding of its own. Reducing the time modulo U_PERIOD*DRIFT_DEN100
    // first removes whole multiples of dmul*U_PERIOD from the quotient,
    // which the mask drops, and it keeps the product inside uint64: the
    // reduced time is under 1.08e14 and dmul is at most 200.
    constexpr uint64_t DRIFT_DEN = TIME_DEN / DRIFT_Q16_PER_S; // 128000
    constexpr uint64_t DRIFT_DEN100 = DRIFT_DEN * 100;         // 12,800,000
    constexpr uint64_t DRIFT_WRAP = static_cast<uint64_t>(U_PERIOD) * DRIFT_DEN100;
    const uint64_t dmul = static_cast<uint64_t>(2 * driftAmt);
    const uint32_t drift =
        static_cast<uint32_t>(((scaledMs % DRIFT_WRAP) * dmul + DRIFT_DEN100 / 2) / DRIFT_DEN100) &
        (U_PERIOD - 1);
    // Reduce before multiplying by 4914. Removing a whole number of line
    // periods from the time cannot change round(4914*ts) mod 32768, and the
    // largest intermediate is (32768*TIME_DEN-1)*4914 < 2.71e18, safely
    // inside uint64_t.
    const uint32_t flow = static_cast<uint32_t>(((scaledMs % (32768ull * TIME_DEN)) * FLOW_Q8_PER_S +
                                                TIME_DEN / 2) / TIME_DEN) & 32767u;
    const uint32_t dens = 128 + (density * 256 + 50) / 100;
    // Cross lines scales the horizontal term: gone at slider 0, exactly the
    // old value at 50, doubled at 100. The clamp is what keeps the doubled
    // value a legal profile index; without it the gather would read past the
    // 256-entry table.
    const int cmul = 2 * cross;
    for (int y = 0; y < h; y++) {
        CursorRow &c = cursor[y];
        const DepthRow &d = depth[y];
        c.du = (d.a * dens) >> 8;
        c.u0 = drift - static_cast<uint32_t>(w >> 1) * c.du;
        const int wv = (d.vq8 + flow) & 32767;
        const int fold = wv <= 16384 ? wv : 32768 - wv;
        const int g = d.lwH - fold;
        int gn = g > 0 ? (g * d.kh) >> 16 : 0;
        gn = (gn * cmul) / 100;
        tone[y].g = static_cast<uint8_t>(gn > 255 ? 255 : gn);
    }
}

// Portable spec: precisely the page's integer pixel loop. Its clamp of the
// palette index is redundant for legal params, and still is with the five
// parameters gm-3vj.43 added: floor shade widens bg to 60..108, amp is
// unchanged at 0..119, PROF=0..255 and dither=-1..1, which puts i in
// 59..228, inside the 256-entry palette, so the clamp is left out rather
// than paid for per pixel. Both normalized profiles stay inside 0..255:
// round(255*65536/lw)*lw differs from 255*65536 by at most lw/2, and line
// width caps lw at 7,741 and lwH at 10,450, both far below the 131,072 at
// which that slack would reach a whole index. Cross lines is the one term
// that can exceed 255 on its own, and frame() clamps it there.
GM_ANIM_IRAM void bandRef(uint16_t *__restrict dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int y = y0; y < y0 + rows; y++) {
        const CursorRow &c = cursor[y];
        const ToneRow &r = tone[y];
        const int16_t *dr = dither + (y & 7) * 8;
        if (r.amp == 0) {
            // Above the depth fade's start the grid does not exist at all.
            for (int x = 0; x < w; x++) {
                *dst++ = palette[r.bg + dr[x & 7]];
            }
            continue;
        }
        uint32_t u = c.u0;
        const uint32_t du = c.du;
        const int lw = c.lw, kv = c.kv, g = r.g, amp = r.amp, bg = r.bg;
        for (int x = 0; x < w; x++) {
            const int wv = static_cast<int>((u >> 8) & 32767);
            const int back = (-wv) & 32767; // 32768-wv, and 0 where wv is 0
            const int fold = wv < back ? wv : back;
            const int hv = lw - fold;
            const int positive = hv > 0 ? hv : 0;
            int n = (positive * kv) >> 16;
            if (g > n) {
                n = g;
            }
            const int i = bg + ((profile[n] * amp) >> 8) + dr[x & 7];
            *dst++ = palette[i];
            u += du;
        }
    }
}

#if GM_BGANIM_GRID_ASM

// One row's worth of the kernel's constants, 16-byte aligned so the pattern
// at offset 32 is a legal ee.vld.128.ip source (that instruction masks the
// low four address bits silently instead of trapping, see ASM_BRIEF.md).
// The Xtensa kernel reads bgd, lwPlus, dstEnd and pat by these exact byte
// offsets, which the static_asserts below lock down; the portable twin
// reads the same fields by name and uses nBlocks instead of dstEnd, because
// a host pointer does not fit the 32-bit field the kernel needs.
struct alignas(16) GridRowAsm {
    uint16_t bgd[8];  // 0:  bg + dither[k], the per-pixel background index
    uint16_t lwPlus;  // 16: fold must reach this for a whole block to be off every line
    uint16_t nBlocks; // 18: w >> 3, the twin's loop count
    uint32_t dstEnd;  // 20: one past the row's last pixel, 32-bit (device only)
    uint32_t pad[2];  // 24: brings pat to the next 16-byte boundary
    uint16_t pat[8];  // 32: the eight pixels an off-line block stores
};
static_assert(offsetof(GridRowAsm, lwPlus) == 16 && offsetof(GridRowAsm, nBlocks) == 18 &&
                  offsetof(GridRowAsm, dstEnd) == 20 && offsetof(GridRowAsm, pat) == 32 &&
                  sizeof(GridRowAsm) == 48,
              "the grid kernel addresses these fields by byte offset");

#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)

// Hand-written Xtensa kernels for band()'s two row shapes.
//
// What the compiler cannot do here is skip work. bandRef() pays the full
// per-pixel chain (fold, reciprocal, smoothstep gather, palette gather) on
// every one of the 230,400 pixels, and the picture does not need it: a
// pixel is only ever off both the vertical and the horizontal line, in
// which case its index is bg + ((PROF[g]*amp)>>8) + dither[x&7], a per-row
// constant plus an 8-periodic dither. That is eight fixed uint16 values for
// the whole row, so an eight-pixel run that is provably off every line is
// one 128-bit store.
//
// fold(wv) = min(wv, 32768-wv) is 1-Lipschitz on the 32768-unit circle, and
// over the eight pixels of a block wv moves by at most ((7*du)>>8)+1 units
// (floor((u+k*du)/256) - floor(u/256) is (k*du)>>8 or one more). So if the
// block's first fold is at least lwPlus = lw + ((7*du)>>8)+1, every pixel in
// it has fold >= lw, hence hv <= 0, hence n = max(nv,g) = g. The test is
// therefore exact, never optimistic: a block that fails it falls through to
// the full chain for all eight pixels, so the kernel is pixel-identical to
// bandRef() by construction rather than by tuning. lw is 16% to 26% of the
// half period at the default line width, so 74% to 84% of blocks take the
// store; the width slider moves that band to 6% to 11% at 0 and 29% to 47%
// at 100, which changes how often the fast path wins and nothing about
// whether it is right.
//
// Cost, at the panel's 480 px row: about 0.9 instructions per pixel on the
// fast path against bandRef()'s compiled 15 plus its load-use stalls, and
// 18 on the slow path. Weighted, about a quarter of the compiled loop's
// work.
//
// Register map for gridLineRowAsm, 13 ARs against the ~14 usable that
// ASM_BRIEF.md documents: dst, u (both updated per block) + du, lw, kv, g,
// amp, prof, pal, rowp (live constants) + t0, t1, t2 (scratch). lwPlus and
// dstEnd stay in the row struct and are loaded once per block rather than
// held, which is what keeps the count under the ceiling; lw is held,
// because the slow path reads it eight times per block.
//
// PIE state: q0 holds the block pattern. The compiler never allocates q
// registers, so no clobber list can name them and none is needed. band()
// runs on the SleepAnim task, never in an ISR, so CP3's context is saved
// lazily per task by FreeRTOS and this kernel never writes CPENABLE (see
// CLAUDE.md's "Animation kernels" section). SAR is untouched: nothing here
// uses a SAR-dependent instruction.

// Fills nBlocks eight-pixel blocks with the row's fixed pattern. Used for
// the rows above the depth fade, where no line exists at all.
GM_ANIM_IRAM __attribute__((noinline)) void gridFillRowAsm(uint16_t *dst, const uint16_t *pat, int nBlocks) {
    uint16_t *dstp = dst;
    const uint16_t *patp = pat;
    asm volatile("ee.vld.128.ip q0, %[pat], 0\n" // eight pixels, pat stays put
                 "loopnez %[n], 1f\n"            // nBlocks is a runtime value, may be 0
                 "ee.vst.128.ip q0, %[dst], 16\n"
                 "1:\n"
                 : [dst] "+r"(dstp), [pat] "+r"(patp)
                 : [n] "r"(nBlocks)
                 : "memory");
}

// The mixed row: per block, one exactness test, then either the 128-bit
// store or eight full pixels.
#define GM_GRID_PIXEL(OFF)                                                                                   \
    "extui  %[t0], %[u], 8, 15\n"      /* wv = (u>>8) & 32767 */                                             \
    "neg    %[t1], %[t0]\n"                                                                                  \
    "extui  %[t1], %[t1], 0, 15\n"     /* back = (-wv) & 32767 */                                            \
    "min    %[t0], %[t0], %[t1]\n"     /* fold */                                                            \
    "sub    %[t0], %[lw], %[t0]\n"     /* hv, may be negative */                                             \
    "mull   %[t0], %[t0], %[kv]\n"                                                                           \
    "l16ui  %[t2], %[rowp], " OFF "\n" /* bgd[k], hoisted into MULL's latency */                             \
    "srai   %[t0], %[t0], 16\n"        /* nv; a negative hv gives <= -1, which the MAX drops */              \
    "max    %[t0], %[t0], %[g]\n"      /* n = max(nv, g) */                                                  \
    "add    %[t1], %[prof], %[t0]\n"                                                                         \
    "l8ui   %[t1], %[t1], 0\n"         /* PROF[n] */                                                         \
    "add    %[u], %[u], %[du]\n"       /* fills the L8UI load-use slot */                                    \
    "mull   %[t1], %[t1], %[amp]\n"                                                                          \
    "srai   %[t1], %[t1], 8\n"                                                                               \
    "add    %[t1], %[t1], %[t2]\n"     /* i = bg + dither + (PROF[n]*amp>>8) */                              \
    "addx2  %[t1], %[t1], %[pal]\n"                                                                          \
    "l16ui  %[t1], %[t1], 0\n"                                                                               \
    "s16i   %[t1], %[dst], " OFF "\n"

GM_ANIM_IRAM __attribute__((noinline)) void gridLineRowAsm(uint16_t *dst, uint32_t u, uint32_t du, int lw,
                                                           int kv, int g, int amp, const uint8_t *prof,
                                                           const uint16_t *pal, const GridRowAsm *rowp) {
    uint16_t *dstp = dst;
    uint32_t up = u;
    int32_t t0, t1, t2;
    asm volatile("addi   %[t0], %[rowp], 32\n"      // &pat, still 16-byte aligned
                 "ee.vld.128.ip q0, %[t0], 0\n"     // q0 = the row's off-line pattern
                 "2:\n"                             // one block of eight pixels
                 "extui  %[t0], %[u], 8, 15\n"      // wv of the block's first pixel
                 "neg    %[t1], %[t0]\n"
                 "extui  %[t1], %[t1], 0, 15\n"
                 "min    %[t0], %[t0], %[t1]\n"     // fold
                 "l16ui  %[t1], %[rowp], 16\n"      // lwPlus
                 "bge    %[t0], %[t1], 3f\n"        // whole block off every line
                 "j      5f\n"                      // BGE is short range, so trampoline
                 "3:\n"
                 "ee.vst.128.ip q0, %[dst], 16\n"   // eight pixels, dst += 16
                 "addx8  %[u], %[du], %[u]\n"       // u += 8*du
                 "4:\n"
                 "l32i   %[t1], %[rowp], 20\n"      // dstEnd
                 "bltu   %[dst], %[t1], 2b\n"
                 "j      6f\n"
                 "5:\n" GM_GRID_PIXEL("0") GM_GRID_PIXEL("2") GM_GRID_PIXEL("4") GM_GRID_PIXEL("6")
                     GM_GRID_PIXEL("8") GM_GRID_PIXEL("10") GM_GRID_PIXEL("12") GM_GRID_PIXEL("14")
                 "addi   %[dst], %[dst], 16\n"
                 "j      4b\n"
                 "6:\n"
                 : [dst] "+r"(dstp), [u] "+r"(up), [t0] "=&r"(t0), [t1] "=&r"(t1), [t2] "=&r"(t2)
                 : [du] "r"(du), [lw] "r"(lw), [kv] "r"(kv), [g] "r"(g), [amp] "r"(amp), [prof] "r"(prof),
                   [pal] "r"(pal), [rowp] "r"(rowp)
                 : "memory");
}

#else

// Portable twins of the two kernels above: same names, same signatures,
// same block dispatch and the same per-pixel arithmetic, so band()'s source
// is one piece whichever branch compiles and the host bench (goldens,
// interlace check, shapes, fuzz) exercises the block test itself, not just
// bandRef(). Only the bodies differ. Copied from the C reference in
// tools/qemubench/tests/anim_grid/main.c, which is what proves the asm
// bodies match these under real Xtensa execution.
void gridFillRowAsm(uint16_t *dst, const uint16_t *pat, int nBlocks) {
    for (int b = 0; b < nBlocks; b++) {
        for (int k = 0; k < 8; k++) {
            dst[k] = pat[k];
        }
        dst += 8;
    }
}

void gridLineRowAsm(uint16_t *dst, uint32_t u, uint32_t du, int lw, int kv, int g, int amp,
                    const uint8_t *prof, const uint16_t *pal, const GridRowAsm *rowp) {
    for (int b = 0; b < rowp->nBlocks; b++) {
        const int wv0 = static_cast<int>((u >> 8) & 32767);
        const int back0 = (-wv0) & 32767;
        const int fold0 = wv0 < back0 ? wv0 : back0;
        if (fold0 >= rowp->lwPlus) {
            for (int k = 0; k < 8; k++) {
                dst[k] = rowp->pat[k];
            }
            u += du * 8u;
            dst += 8;
            continue;
        }
        for (int k = 0; k < 8; k++) {
            const int wv = static_cast<int>((u >> 8) & 32767);
            const int back = (-wv) & 32767;
            const int fold = wv < back ? wv : back;
            int n = ((lw - fold) * kv) >> 16;
            if (n < g) {
                n = g;
            }
            const int i = rowp->bgd[k] + ((prof[n] * amp) >> 8);
            dst[k] = pal[i];
            u += du;
        }
        dst += 8;
    }
}

#endif // __XTENSA__ && !GM_BGANIM_NO_ASM

// Kernel-dispatching band(): the same algorithm as bandRef(), with the
// per-pixel loop replaced by the block kernel. Falls back to bandRef() for
// any geometry the 128-bit store cannot take: the panel is 480 or 240 wide
// and its band buffers are 64-byte aligned, so every row start is 16-byte
// aligned and the fallback never runs on the device, but the bench and the
// fuzzer do hand this function odd widths.
GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
    if (w < 8 || (w & 7) != 0 || (reinterpret_cast<uintptr_t>(dst) & 15) != 0) {
        bandRef(dst, y0, rows, w, tMs, p);
        return;
    }
    GridRowAsm rowp;
    rowp.nBlocks = static_cast<uint16_t>(w >> 3);
    for (int row = 0; row < rows; row++) {
        const int y = y0 + row;
        uint16_t *out = dst + static_cast<size_t>(row) * w;
        const CursorRow &c = cursor[y];
        const ToneRow &r = tone[y];
        const int16_t *dr = dither + (y & 7) * 8;
        // The index every pixel that is off both lines takes: n == g there,
        // so PROF[g] is a row constant and only the dither varies with x&7.
        const int off = (profile[r.g] * r.amp) >> 8;
        for (int k = 0; k < 8; k++) {
            const int bgd = r.bg + dr[k];
            rowp.bgd[k] = static_cast<uint16_t>(bgd);
            rowp.pat[k] = palette[bgd + off];
        }
        rowp.dstEnd = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(out + w));
        if (r.amp == 0) {
            // off is 0 here, so the pattern is bandRef()'s background row.
            gridFillRowAsm(out, rowp.pat, w >> 3);
            continue;
        }
        // ((7*du)>>8)+1 bounds how far fold can move across a block. Capped
        // at 32767 so a very large du simply sends every block to the slow
        // path instead of overflowing the uint16 field.
        int lwPlus = c.lw + static_cast<int>((7u * c.du) >> 8) + 1;
        if (lwPlus > 32767) {
            lwPlus = 32767;
        }
        rowp.lwPlus = static_cast<uint16_t>(lwPlus);
        gridLineRowAsm(out, c.u0, c.du, c.lw, c.kv, r.g, r.amp, profile, palette, &rowp);
    }
}

#else

// Flag off: band() is bandRef() byte for byte, so the host goldens compare
// the same code either way.
GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
    bandRef(dst, y0, rows, w, tMs, p);
}

#endif // GM_BGANIM_GRID_ASM

void release() {
    releaseTable(cursor, static_cast<size_t>(allocH) * sizeof(CursorRow));
    releaseTable(tone, static_cast<size_t>(allocH) * sizeof(ToneRow));
    releaseTable(profile, 256);
    releaseTable(palette, 256 * sizeof(uint16_t));
    releaseTable(dither, 64 * sizeof(int16_t));
    releaseTable(depth, static_cast<size_t>(allocH) * sizeof(DepthRow));
    allocW = allocH = 0;
    lastToneKey = -1;
    lastWidth = -1;
    lastThemeGen = 0;
}

} // namespace

extern const BgAnimation bg_anim_grid;
const BgAnimation bg_anim_grid = {
    "grid",
    "Grid",
    {{"speed", "Speed", 50},
     {"density", "Grid density", 50},
     {"lines", "Line strength", 58},
     {"width", "Line width", 50},
     {"cross", "Cross lines", 50},
     {"reach", "Grid reach", 50},
     {"shade", "Floor shade", 50},
     {"drift", "Side drift", 50}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
