#ifndef GAGGIMATE_SIM

// "Aurora": two domain-warped sine curtains over a dark sky. The warp terms
// depend only on y and t (per-row constants); per pixel is two DDS phase
// accumulators indexing pre-weighted sine LUTs, a squared-intensity term, a
// row-constant scale, ordered dither, and one final LUT read that already
// has the sky color baked in. Design: anim-celestial (Fable), 2026-08-15.
//
// Perf pass 1 (opt-aurora, 2026-08-15): host band_ms 0.936 -> 0.277 (two
// per-pixel sinLut() calls were uninlinable function calls).
// Perf pass 2 (opt-aurora, 2026-08-15): host band_ms 0.277 -> ~0.19 (hoisted
// the per-row __fixsfdi wraparound cast to frame(), padded rowLUT to remove
// per-pixel clip branches, folded ordered dither into 8 row-constant
// pointers).
// Perf pass 3 (asm, round 1, 2026-09-04): wrote auroraPixelsAsm(), a
// hand-scheduled scalar Xtensa kernel, on the theory that pass 2's 8 live
// rowLUTAtBit pointers were spilling through the unrolled x loop (more than
// the ~13 usable address registers a windowed-ABI frame leaves). Round-2
// device measurement (below) showed that theory was only partly right: the
// kernel is correct (rung 4: 0 mismatched pixels, 5760 bands x 3 param
// sets) but only 1.03-1.09x over the compiled C++ loop it replaced, because
// both do the SAME THREE GATHERS per pixel (two sine tables, one color
// table) and gathers, not instruction count, are what the device pays for.
// Rescheduling instructions around an unchanged memory access pattern
// barely moves the number.
//
// Perf pass 4 (round 2, 2026-09-04): the actual lever, per the round-2
// device numbers, was the algorithm's memory access pattern, not the
// kernel's instruction schedule:
//
//  1. Table placement. wLut1/wLut2/glowLUT now come from bganim::allocHot()
//     (a fixed 12 KB internal slab, 9,216 B of which is this animation's
//     share once the shared sinLut/cosTableF take their 3,072 B) instead of
//     alloc() (PSRAM, unconditionally, as of this round -- see
//     BgAnimCommon.h's placement-API comment). The three tables total
//     8,704 B (4,096 + 4,096 + 512), all 16-byte-aligned already, so they
//     fit the slab with 512 B to spare and no shrinking was needed. (Since
//     the row-LUT cache fix, see g_rowLUT, the slab holds wLut1, wLut2 and
//     the 768 B row table, 8,960 B, and glowLUT is in PSRAM.) Before
//     this round, table placement was decided against the free internal
//     pool at init() time, so the SAME table could land in SRAM on one boot
//     and PSRAM on the next depending on radio/heap state; allocHot makes
//     the placement (and therefore the band time) the same on every boot.
//  2. IRAM. Perf pass 3 put band()/auroraPixelsAsm() in IRAM (+794 B static
//     internal RAM) on the theory that flash icache misses under BLE/WiFi
//     coex cost real time -- copied from AnimEmber.cpp's rationale without
//     measuring it for THIS animation. Round-2 measurement showed the whole
//     pass 3 kernel bought only 1.03-1.12x, most of which is explained by
//     placement and the reordering below, not by IRAM residency, so the
//     794 B is dropped: it is static internal RAM (the pool WiFi's frame
//     copies come from) with no measured justification here.
//  3. The per-pixel algorithm. auroraPixelsAsm's 25 instructions/pixel
//     (round 1) did two independent sine-table gathers (w1[idx1], w2[idx2])
//     and one dependent color gather (rowLUT[scaledSq+dither]) EVERY pixel.
//     The two-curtain sum v(x) is a smooth, low-frequency field (the
//     fastest curtain's per-pixel phase step is ~0.026 rad, so two pixels
//     span ~3 degrees of a slowly domain-warped sine -- see the error bound
//     below), so this pass evaluates v(x) -- and everything downstream of
//     it through the row-constant square/scale -- at every OTHER x (a
//     coarse column grid, spacing 2) and linearly interpolates the ROWLUT
//     INDEX (scaledSq, not the raw field) for the pixel in between. That:
//       - halves the two sine-table gathers (the two largest tables, 4 KB
//         each) to one gather-pair per TWO pixels instead of per pixel,
//       - skips the clamp/square/scale chain entirely for the interpolated
//         half of the pixels (they only do one add, one shift, the dither
//         lookup, and the final color gather),
//       - keeps the final color gather (rowLUT) and the dither lookup
//         per-pixel, unchanged, since they are cheap (rowLUT is under 1 KB,
//         also in the hot slab range if it were heap-allocated, though it
//         stays a stack local here as it did before) and the interpolated
//         index still needs a real color for that exact pixel.
//     Interpolating the SQUARED, SCALED value (not v itself) matters for
//     fidelity: v is what gets clamped and squared, a nonlinear step, so
//     interpolating v and then squaring would double the interpolation
//     error near the top of the curve; interpolating scaledSq directly
//     interpolates the quantity that is itself already smooth (both
//     endpoints are valid, correctly clamped samples, so there is no
//     discontinuity to interpolate across, only curvature). See the golden
//     diff numbers in this pass's report for the measured error.
//
// bandRef below implements this same (round-2) algorithm in portable C++
// and stays the pixel-exact spec the device equivalence test
// (SleepAnimation::runAnimTest, /api/debug/animtest) checks
// auroraPixelsAsm() against; it is NOT bit-exact against the pre-round-2
// golden frames (a coarse-grid approximation, by construction, is not
// identical to the exact per-pixel field), so the golden tolerance (mean
// <= 3.0, max <= 48 RGB) is what this round's `make check` verifies instead
// of the exact match perf passes 1-3 held.
//
// Perf pass 5 (round 5, 2026-09-04, kb.py device-in-the-loop): the flashed
// round-4 kernel measured min_ms 25.95-26.06 on the bench board before this
// pass (unchanged blob upload, three repeats). Instruction count was
// already tight (34/pair, matching xtensa-asm14's disassembly) and every
// load-then-ALU-use pair had a filler instruction between them except one:
// the v = w1[idx1] + w2[idx2] gather's `add` sat directly behind w2's l32i,
// which is exactly the one-cycle load-use stall ASM_BRIEF.md documents.
// Moving the "vc = max(v, 0)" zero constant's movi to fill that gap (using
// t3, idle at that point in the iteration, instead of reusing t2 right
// after its own load) costs nothing in instruction count and measured
// min_ms 25.44-25.45 over three repeats, about 1.9% down and reproducible
// tightly enough (both repeats agreeing to 0.01 ms) to trust despite being
// under this round's 3% noise-vs-signal bar for a single run. Two further
// ideas, both plausible from the same reasoning, did NOT hold up on the
// device and are commented in place where they were tried: giving the
// second mull (sq*rowScale) a second latency-filler measured no change at
// all, meaning this kernel's mull results are usable by the very next
// instruction with no extra hiding needed (unlike loads); and moving the
// df[bit_odd] load earlier to give the color_even store a filler measured
// WORSE, meaning a load feeding a store's data operand is not the same
// hazard as a load feeding an ALU operand on this core. Net for the round:
// one real fix, two rejected by measurement, kernel still 34 instructions
// per pair. See this pass's report for the arithmetic-floor argument for
// why this is close to what this schedule, at this register budget, can
// deliver.
//
// Redesign, 2026-09-05 (design worker aurora2): the row-LUT cache fix above
// (g_rowLUT, g_lastBgIdx) took the rig from 25.5 to 17.2 ms per frame with
// identical pixels. This pass changes what band() renders, not how it
// schedules the rendering: for each y/y+1 row pair it computes the full row
// (coarse-grid sample, clamp/square/scale, dither, rowLUT gather) once and
// duplicates it into the other row with memcpy, instead of running the
// per-pixel work on both. The two curtains are a slow, vertically smooth
// field (warp1/warp2 change by only 0.021 and 0.013 rad per row, and env is
// a smooth ramp in |y|), so a pair's two rows already differed by a small
// fraction of a rowLUT step; collapsing that difference to zero trades a
// little vertical smoothness for about half the per-pixel work and, on top
// of the caching fix (each computed row now also costs one computeRowState
// call and one buildRowLUT check instead of two), takes the rig from 17.2
// to 9.5 ms. Golden diff against the pre-redesign frames (030/120/210) is
// mean 0.65 to 0.87 of 255, well under the file's existing coarse-grid
// tolerance. The dither phase stays keyed on the row PAIR, y >> 1, not on y
// itself: keying it on y would only ever show four of BAYER8's eight row
// phases (y is always even at the point renderAuroraRow* is called with it,
// see below), and those four rows share the same column parity in the
// matrix, so the grain lines up into visible vertical stripes through the
// gradient instead of a checkerboard (found in frame review).
//
// A first cut of this pass computed the pair's row state from whichever row
// happened to be first in a given band() call and fell back to a lone row's
// own state otherwise, which tools/animbench/interlace_check.cpp caught: a
// row's content and dither phase must depend only on its own absolute y,
// never on which other rows the same call happened to also request.
// SleepAnimation.cpp has a real row-level interlace path that calls band()
// with rows==1, one row at a time, rendering only every other row each
// frame, so a lone-row call is not a hypothetical shape, it is production
// traffic. The fix: every row derives ySrc = y & ~1 (its pair's even row)
// and renders ySrc's content, whether or not its partner is in this call;
// the memcpy from one row to the other is only ever an optimization used
// when both rows of a pair land in the same call (production's usual
// rows==2, y0 even, so the common case still gets it), never a source of
// different output depending on call shape. Confirmed with
// render_one.cpp's --shapes mode (an interlace_check for one candidate
// descriptor, added for the aurora2/lava2 redesign passes) before this
// touched src/, and again here with tools/animbench/interlace_check.cpp
// across the whole fleet.
//
// Parameters, 2026-09-10 (gm-3vj.7): three sliders became seven. Height,
// Spread, Glow and Drift were fixed constants in frame(), computeRowState and
// buildGlowLUT; they are now sliders that reach exactly those constants at
// the default 50, so the goldens are unchanged (mean 0.000, max 0 on all
// three frames). None of them touches the pixel loop or auroraPixelsAsm: two
// feed the row envelope, one feeds the phase bases, one feeds the glow
// colours. See the g_envCenter block below for why each maps as an offset
// from the midpoint rather than as a span from a low end.
#include "BgAnim.h"
#include "BgAnimClock.h"
#include "BgAnimCommon.h"
#include <math.h>
#include <string.h>

namespace {
using namespace bganim;

uint16_t *glowLUT = nullptr; // [256 intensity] -> RGB565 glow color (theme-baked)
uint32_t lastThemeGen = 0xFFFFFFFF;
// The per-row colour table and the background index it was built for. These
// used to be locals of band() and bandRef(), which was a caching bug: a local
// lastBgIdx = -1 forces buildRowLUT's rebuild on every call, and with the
// production BAND_H of 2 that is ~240 rebuilds a frame instead of the ~10 the
// background actually changes (bgIdx = y/48). Measured 25.5 -> 17.2 ms per
// frame on the rig with identical pixels once the cache survived across
// calls. The table lives in the hot slab (read per pixel); frame() resets
// the index so a theme or glow rebuild is never served from a stale table.
uint16_t *g_rowLUT = nullptr;
int g_lastBgIdx = -1;

// f1 = 0.026, f1b = 0.017 rad/px -> phase steps in Q8 ticks of the 1024-LUT.
constexpr float TICKS = 1024.0f * 256.0f / 6.2831853f; // rad -> Q8 LUT ticks
constexpr uint32_t STEP1 = static_cast<uint32_t>(0.026f * TICKS);
constexpr uint32_t STEP2 = static_cast<uint32_t>(0.017f * TICKS);

// The asm kernel below advances each phase by 2*STEP per pair (one coarse-
// grid sample every 2 pixels). 2*STEP1 = 2168 exceeds MOVI's 12-bit signed
// range (-2048..2047), so a register can't be preloaded with it in one
// instruction; rather than spend a register materializing it (round-1's
// approach, see the kernel comment's history note), the doubled step is
// split into an ADDMI part and an ADDI remainder and applied as two
// immediate-only instructions with no register operand at all. Unlike
// ADDI, whose assembly-text immediate is the literal value added, ADDMI's
// assembly-text immediate is ALSO the literal value added (not a
// pre-shifted field, confirmed against this toolchain's actual encoding by
// objdump on a compiled kernel: the disassembly shows the same real value
// back, and a first attempt at this file that passed the imm8 field value
// instead of the real value -- 8 meaning "2048" -- silently assembled and
// then added only 8, corrupting the whole kernel's output at runtime with
// no compile-time signal), and must be a multiple of 256 (its hardware
// field is imm8<<8, range -32768..32512 in steps of 256). HI rounds DSTEP
// to the nearest multiple of 256 so LO always lands inside ADDI's
// -128..127 range.
constexpr int32_t DSTEP1 = 2 * static_cast<int32_t>(STEP1);
constexpr int32_t DSTEP1_HI = ((DSTEP1 + 128) >> 8) << 8;
constexpr int32_t DSTEP1_LO = DSTEP1 - DSTEP1_HI;
constexpr int32_t DSTEP2 = 2 * static_cast<int32_t>(STEP2);
constexpr int32_t DSTEP2_HI = ((DSTEP2 + 128) >> 8) << 8;
constexpr int32_t DSTEP2_LO = DSTEP2 - DSTEP2_HI;
static_assert(DSTEP1_LO >= -128 && DSTEP1_LO <= 127, "ADDI immediate out of range");
static_assert(DSTEP2_LO >= -128 && DSTEP2_LO <= 127, "ADDI immediate out of range");
static_assert(DSTEP1_HI % 256 == 0 && DSTEP1_HI >= -32768 && DSTEP1_HI <= 32512, "ADDMI immediate out of range");
static_assert(DSTEP2_HI % 256 == 0 && DSTEP2_HI >= -32768 && DSTEP2_HI <= 32512, "ADDMI immediate out of range");

// Curtain weights (0.62/0.38 in Q7) are compile-time constants, so pre-scaling
// the shared sine LUT by them once (at init) turns the per-pixel "v1*635"/
// "v2*393" multiplies into plain array reads. The final ">>7" that undoes the
// Q7 weighting is also folded into the table here (each entry pre-shifted)
// rather than applied once to the summed v in band() -- (a>>7)+(b>>7) differs
// from (a+b)>>7 by at most 1 ULP (each addend's low 7 bits are truncated
// separately instead of the sum's), invisible against the +-8 ordered-dither
// noise already added downstream, and it removes a per-pixel shift from the
// hottest loop in the file (perf pass 3, opt-aurora).
constexpr int32_t W1 = 635, W2 = 393;
int32_t *wLut1 = nullptr; // [1024] (sin1024(i) * W1) >> 7, in the hot slab
int32_t *wLut2 = nullptr; // [1024] (sin1024(i) * W2) >> 7, in the hot slab

// v = wLut1+wLut2 ranges about +-4112 (512*(W1+W2)>>7); the clamp-then-square
// step below is branchless rather than a table read (see perf pass 2's
// history: a 16 KB sqLUT indexed by a per-pixel-random v lived in PSRAM and
// cost a round trip per pixel for one multiply and one shift).
constexpr int32_t V_MAX = (512 * (W1 + W2)) >> 7;

// Row-color-LUT sizing, namespace scope so both bandRef and the asm dispatch
// path below size their local rowLUT[] arrays identically. See the
// pre-clip-range comment by ROWLUT_SIZE's use, further down, for the
// derivation.
constexpr int32_t SQ_MAX = (V_MAX * V_MAX) >> 12;       // largest possible vc*vc>>12
constexpr int32_t INTEN14_MAX = 358;                    // p[1]=100 -> (100/100.0f)*1.4f*256.0f, truncated
constexpr int32_t ROWLUT_PAD = 8;                       // covers dither's -8 low excursion
constexpr int32_t ROWLUT_SIZE = ((SQ_MAX * INTEN14_MAX) >> 12) + 7 + ROWLUT_PAD + 1 + 8;

float g_A1 = 0, g_A2 = 0;
int32_t g_inten14 = 0; // intensity * 1.4 in Q8

// Animation time (BgAnimClock.h, gm-bzu.50). Not reset by release(), so the
// full/half switch keeps the curtains where they were. Each of the four
// motions below keeps its own phase. The coefficients are the old ones, in
// radians per second of t, where t was tMs * 0.001 * 1.05 * speed:
// 1.05e-3 converts them to radians per animation millisecond. The base rate
// was 0.45 before the speed calibration (gm-33fm, 2026-09-12); 1.05 is that
// rate times 2.33, so that Speed 50 moves this animation about as much per
// second as every other animation at Speed 50.
AnimClock g_clock;
// T_PER_MS is formed from the page's float constants, 0.001f * 1.05f, and
// each coefficient below is the page's float too, so these are the page
// chain's own rates and the handover at PAGE_EXACT_Q16 does not jump.
constexpr double T_PER_MS = static_cast<double>(0.001f) * static_cast<double>(1.05f);
constexpr uint64_t RATE_WARP1 = oscRateQ48(static_cast<double>(0.5f) * T_PER_MS);
constexpr uint64_t RATE_WARP2 = oscRateQ48(static_cast<double>(0.44f) * T_PER_MS); // subtracted, as the page writes it
// The bases are t * coeff * TICKS ticks on the page, with TICKS the float
// 2^18 / 2pi, and 2^18 ticks are a turn here; this is that rate in radians.
constexpr double BASE_RAD_PER_TICK = 6.283185307179586 / 262144.0;
constexpr uint64_t RATE_BASE1 =
    oscRateQ48(static_cast<double>(0.12f) * T_PER_MS * static_cast<double>(TICKS) * BASE_RAD_PER_TICK);
constexpr uint64_t RATE_BASE2 =
    oscRateQ48(static_cast<double>(0.07f) * T_PER_MS * static_cast<double>(TICKS) * BASE_RAD_PER_TICK);
// This frame's warp phases in radians. The second is subtracted in
// computeRowState, because the page writes y * 0.013 - t * 0.44 and adding a
// negative-rate phase rounds differently.
float g_warpPh1 = 0, g_warpPh2 = 0;
// How long frame() follows the page's own float arithmetic (gm-pciz). The
// page forms t in float seconds and chains float products from it, so the
// only way to draw its frames bit for bit is to run the same chain on the
// clock's time. That chain loses resolution as t grows, so past 2048 s of
// animation time (34 minutes at Speed 50, where the fastest warp phase is near
// 1100 rad and the bases near 1.1e7 ticks) frame() takes the Q48 rates
// instead. The handover moves a warp phase by about 0.1 mrad and a base by
// about one tick of the 256 per table entry, once; the rates then never lose
// precision and never jump at a wrap.
constexpr int64_t PAGE_EXACT_Q16 = 2048000ll << 16;
// Q16 milliseconds as the page's float seconds: milliseconds rounded to
// float, times 0.001f. Exact whole milliseconds at Speed 50.
inline float pageSecondsF(int64_t q16) {
    return static_cast<float>(static_cast<double>(q16) * (1.0 / 65536.0)) * 0.001f;
}
// The sideways travel's own time, Q16 animation milliseconds, scaled by the
// Drift param (p6) as well as by Speed. It is a separate accumulator because
// drift can be negative and AnimClock only runs forward: frame() adds
// wall step x speed x drift in two's complement, and the product with a Q48
// rate wraps modulo 2^64, a whole number of turns, the same as
// oscTurnQ32's. A drift change bends the travel instead of moving it.
uint64_t g_driftQ16 = 0;

// Size. The curtains are drawn in render pixels: STEP1/STEP2 per column and
// 0.021/0.013 rad per row. On the half-resolution path (240 or 233 px,
// expanded 2x) that would show curtains twice as wide and twice as tall on
// screen, and y / 480 would put the top half of the envelope and of the sky
// ramp across the whole screen (bganim F3, gm-bzu.50). g_invH normalises y
// exactly. g_zoom is the integer spatial scale: 1 at 480 and 466, 2 at the
// half widths. The asm kernel steps by the fixed STEP1/STEP2, so the column
// frequency doubles through the tables instead: at zoom 2, wLut1/wLut2 hold
// sin(2*theta) and each row's phase is halved, which reads the same curve
// at twice the rate (see computeRowState).
int g_zoom = 1;
float g_invH = 1.0f / 480.0f;

// Params 3 to 6, added 2026-09-10 (gm-3vj.7). Every one of them acts on a
// frame-constant or a row-constant term: three feed computeRowState and the
// phase bases, one feeds the glow table. Nothing here reaches the pixel loop,
// so auroraPixelsAsm below is untouched and bandRef stays its exact spec.
//
// Each slider maps to the constant it replaced as an OFFSET from the
// midpoint, so at the default 50 the added term is exactly 0.0f in float and
// the picture is bit for bit what it was before the params existed. Writing
// them as "lo + (p/100)*span" instead would land a rounding error on the
// default and move every golden frame by a least significant bit.
float g_envCenter = 0.32f;     // p3 Height: where down the panel the band sits
float g_envInv = 1.0f / 0.85f; // p4 Spread: reciprocal of the band's height
float g_glowGain = 2.2f;       // p5 Glow: how fast the ramp reaches full colour
float g_drift = 1.0f;          // p6 Drift: rate and direction of sideways travel
// The p5 value glowLUT currently holds. 0xFF is not a reachable parameter
// value (0 to 100), so it forces the first frame after init() to rebuild.
uint8_t g_glowP = 0xFF;

// Per-row phase = TICKS*(warp(y) + t*coeff). t*coeff is frame-constant (same
// for all 480 rows), but t itself is proportional to uptime and unbounded, so
// TICKS*t*coeff can exceed int32 range after long enough uptime. The old code
// cast the *whole* per-row sum through int64 to get correct uint32 wraparound
// (see boot-loop history in other anims' OTA notes) -- but doing that 64-bit
// libcall (__fixsfdi) 2x/row * 480 rows = 960x/frame was the single biggest
// remaining per-frame cost. Splitting the sum algebraically fixes this: the
// int64-safe wraparound conversion happens ONCE per frame (here) for the
// t*coeff term only; per-row, warp(y)*TICKS is bounded (|warp|<=3, so
// |warp*TICKS|<~125000, well inside int32) and needs only a plain trunc-to-
// int32 (one hardware instruction, no libcall). uint32 addition of the two
// wraps identically to converting the combined sum, so long-uptime behavior
// is unchanged.
uint32_t g_phBase1 = 0, g_phBase2 = 0;
// The paragraph above predates the clock: t no longer grows with uptime, and
// the bases now come from g_driftQ16 times a Q48 rate (a full turn is 2^18
// Q8 ticks, the top 18 bits of the Q32 turn). The split between a per-frame
// base and a bounded per-row warp term still holds.

// Curtain color rides the theme's mid-to-bright range; the fade ramp keeps
// low intensities near-black so the additive blend stays subtle. g_glowGain
// (p5) is how steep that ramp is: below the default the brightest curtain
// never reaches the full theme colour and the whole curtain reads soft and
// dim, above it the ramp saturates part way up and the curtain reads as a
// hard bright sheet with a thin fade at its edge.
void buildGlowLUT() {
    for (int i = 0; i < 256; i++) {
        uint8_t c[3];
        themeRGB(40 + ((i * 215) >> 8), c);
        const float scale = fminf(1.0f, (i / 255.0f) * g_glowGain);
        glowLUT[i] = rgb565(clamp8f(c[0] * scale), clamp8f(c[1] * scale), clamp8f(c[2] * scale));
    }
}

bool init(int w, int h) {
    const int16_t *lut = sinLut();
    if (lut == nullptr) {
        return false;
    }
    // Round 2: allocHot(), not alloc(), for the tables read every pixel or
    // every row (BgAnimCommon.h's placement criterion): wLut1, wLut2 and,
    // since the row-LUT cache fix, g_rowLUT. Total 8,960 B (4,096 + 4,096 +
    // 768) against the slab's 9,216 B per-animation share. glowLUT moved the
    // other way, to alloc() (PSRAM): it is only read while rebuilding
    // g_rowLUT, ~10 times a frame, 256 entries each, and its 512 B were what
    // the 768 B row table needed. alloc() is PSRAM unconditionally as of
    // round 2, so the per-pixel tables must not fall back to it: this file's
    // header measured the 46.7 -> 67.6 ms penalty when they did.
    if (glowLUT == nullptr) {
        glowLUT = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
    }
    if (wLut1 == nullptr) {
        wLut1 = static_cast<int32_t *>(allocHot(SIN_N * sizeof(int32_t)));
    }
    if (wLut2 == nullptr) {
        wLut2 = static_cast<int32_t *>(allocHot(SIN_N * sizeof(int32_t)));
    }
    if (g_rowLUT == nullptr) {
        g_rowLUT = static_cast<uint16_t *>(allocHot(ROWLUT_SIZE * sizeof(uint16_t)));
        if (g_rowLUT == nullptr) {
            g_rowLUT = static_cast<uint16_t *>(alloc(ROWLUT_SIZE * sizeof(uint16_t)));
        }
    }
    g_lastBgIdx = -1;
    if (glowLUT == nullptr || wLut1 == nullptr || wLut2 == nullptr || g_rowLUT == nullptr) {
        return false;
    }
    // Half widths (233 to 240) take zoom 2; 466 and 480 take 1.
    g_zoom = (w < h ? w : h) < 360 ? 2 : 1;
    g_invH = 1.0f / static_cast<float>(h);
    for (int i = 0; i < SIN_N; i++) {
        const int j = (i * g_zoom) & (SIN_N - 1);
        wLut1[i] = (static_cast<int32_t>(lut[j]) * W1) >> 7;
        wLut2[i] = (static_cast<int32_t>(lut[j]) * W2) >> 7;
    }
    buildGlowLUT();
    lastThemeGen = themeGen();
    return true;
}

void frame(uint32_t tMs, int, int, const uint8_t p[BG_ANIM_PARAMS]) {
    // p5 changes the colours in glowLUT, so the table is rebuilt on the same
    // terms a theme change rebuilds it: only when the value it was built for
    // has moved. 256 entries with a themeRGB() sample each is too much to
    // spend on every frame, and it buys nothing while the slider sits still.
    g_glowGain = 2.2f + (static_cast<int>(p[5]) - 50) * 0.032f; // 0.6 .. 3.8
    if (themeGen() != lastThemeGen || p[5] != g_glowP) {
        buildGlowLUT();
        lastThemeGen = themeGen();
        g_glowP = p[5];
    }
    // One real rebuild per frame at most per background segment: see g_rowLUT.
    // This also republishes the new glow colours through every row table.
    g_lastBgIdx = -1;
    // Speed calibration, gm-33fm 2026-09-12: the base rate carries a
    // deliberate factor so that Speed 50 moves this animation about as
    // much per second as every other animation at Speed 50 (T_PER_MS).
    const float spd = speedMul(p[0]);
    const uint32_t wallStep = g_clock.advance(tMs, spd);
    if (g_clock.simQ16 < static_cast<uint64_t>(PAGE_EXACT_Q16)) {
        const float t = pageSecondsF(static_cast<int64_t>(g_clock.simQ16)) * 1.05f;
        g_warpPh1 = t * 0.5f;
        g_warpPh2 = t * 0.44f;
    } else {
        g_warpPh1 = oscRad(g_clock, RATE_WARP1);
        // Plus two turns, so y * 0.013 - ph2 + 1.7 stays negative on every
        // row, as it is on the page once t * 0.44 has grown past 8: rowCos
        // truncates toward zero, so a sign change would move part of the
        // rows by one table entry at the handover.
        g_warpPh2 = oscRad(g_clock, RATE_WARP2) + 2.0f * 6.2831853f;
    }
    g_A1 = 0.6f + (p[2] / 100.0f) * 2.4f;
    g_A2 = 0.4f + (p[2] / 100.0f) * 1.6f;
    g_inten14 = static_cast<int32_t>((p[1] / 100.0f) * 1.4f * 256.0f);
    // p3 moves the band up and down the panel, p4 makes it shorter or taller.
    // Both are read once per row by computeRowState. The spread never reaches
    // zero, so the reciprocal is always finite; env is clamped at 0 below it,
    // and it still peaks at exactly 1, so rowScale keeps the bound ROWLUT_SIZE
    // was derived from.
    g_envCenter = 0.32f + (static_cast<int>(p[3]) - 50) * 0.006f;         // 0.02 .. 0.62
    g_envInv = 1.0f / (0.85f + (static_cast<int>(p[4]) - 50) * 0.011f);   // band 0.30 .. 1.40 tall
    // p6 scales how far the curtains travel sideways per second. Below about
    // 38 it goes negative and they drift the other way; the warp terms in
    // computeRowState keep waving either way, so nothing freezes at 0. The
    // range is wide because the travel it scales is slow: the default is
    // about 2 px a second, so a 2x end is a difference nobody notices in a
    // glance. At 5x it is 10 px a second, and Speed multiplies on top.
    g_drift = 1.0f + (static_cast<int>(p[6]) - 50) * 0.08f; // -3.0 .. 5.0
    // speed x drift in Q16 is at most 6.7 x 5 x 65536, about 2.2e6, so it
    // fits int32 and the product with a wall step fits int64 for any step.
    const int32_t driftQ16 = static_cast<int32_t>(lroundf(spd * g_drift * 65536.0f));
    g_driftQ16 += static_cast<uint64_t>(static_cast<int64_t>(wallStep) * driftQ16);
    // Q8 ticks of the 1024-entry LUT: a full turn is 2^18, the top 18 bits of
    // the Q32 turn. uint32 wraps whole turns, so the bases stay exact.
    // While the travel is short the bases are the page's float chain on the
    // travel's own time, which is the page's t * drift at the default drift.
    const int64_t driftSigned = static_cast<int64_t>(g_driftQ16);
    if (driftSigned < PAGE_EXACT_Q16 && driftSigned > -PAGE_EXACT_Q16) {
        const float tD = pageSecondsF(driftSigned) * 1.05f;
        g_phBase1 = static_cast<uint32_t>(static_cast<int64_t>(tD * 0.12f * TICKS));
        g_phBase2 = static_cast<uint32_t>(static_cast<int64_t>(tD * 0.07f * TICKS));
    } else {
        g_phBase1 = static_cast<uint32_t>((g_driftQ16 * RATE_BASE1) >> 32) >> 14;
        g_phBase2 = static_cast<uint32_t>((g_driftQ16 * RATE_BASE2) >> 32) >> 14;
    }
}

// Pure helpers shared by both bandRef and the asm dispatch path below (both
// need identical row-constant tables; these are the only piece it is safe
// to share, since they take no hidden state beyond their arguments).
// The page adds (BAYER8 - 31.5) / 63 * 0.03 to an intensity of 0..1, which is
// a swing of 3.8 levels of 255 either way. The fold used to shift by 2, a
// swing of -8..+7, twice the design's (gm-pciz); shifting by 3 gives -4..+3,
// the design's amplitude within the integer rounding. ROWLUT_PAD still
// covers the low end.
void buildDitherFold(int32_t out[64]) {
    for (int i = 0; i < 64; i++) {
        out[i] = ROWLUT_PAD + ((static_cast<int32_t>(BAYER8[i]) - 32) >> 3);
    }
}

// Round 5 tried packing two adjacent ditherFold entries (an even/odd pixel
// pair; the asm kernel always reads them together) into one 32-bit word,
// even in the low half and odd in the high half, so the kernel would need
// one l32i plus two extui unpacks instead of two l32i reads. Bit-exact
// (kb.py: same pixels as band()) but measured WORSE on the device (min_ms
// 26.17, consistent over two repeats, vs 25.44-25.45 without it): the net
// instruction count went from 34 to 35 per pair (one load traded for two
// extui), and the removed load was not expensive enough to pay for the
// extra instruction. Reverted; do not reapply without a fresh measurement
// that shows the load, not the instruction count, is the bottleneck.

void buildRowLUT(uint16_t out[ROWLUT_SIZE], int bgIdx) {
    uint8_t bg[3];
    themeRGB(bgIdx, bg);
    const int bgR5 = bg[0] >> 3, bgG6 = bg[1] >> 2, bgB5 = bg[2] >> 3;
    for (int i = 0; i < ROWLUT_SIZE; i++) {
        // Undo the pad, then clamp to the real [0,255] intensity range --
        // entries outside it just replicate the black or full-glow
        // endpoint, which is what the old clip-then-index sequence produced
        // per pixel.
        int inten = i - ROWLUT_PAD;
        if (inten < 0) {
            inten = 0;
        } else if (inten > 255) {
            inten = 255;
        }
        const uint16_t glow = glowLUT[inten];
        int r = bgR5 + ((glow >> 11) & 0x1F);
        int g = bgG6 + ((glow >> 5) & 0x3F);
        int b = bgB5 + (glow & 0x1F);
        if (r > 0x1F) {
            r = 0x1F;
        }
        if (g > 0x3F) {
            g = 0x3F;
        }
        if (b > 0x1F) {
            b = 0x1F;
        }
        out[i] = static_cast<uint16_t>((r << 11) | (g << 5) | b);
    }
}

// Row-constant setup shared by bandRef and band(): warp, env, rowScale,
// bgIdx-gated rowLUT rebuild, phase bases, Bayer row offset. Both callers
// need the identical numbers (bandRef is the spec the kernel is checked
// against pixel for pixel), so this is the one row-setup copy; only the x
// loop after it differs (compiled C++ walk vs the asm kernel call).
struct RowState {
    uint32_t ph1, ph2;
    int32_t rowScale;
    int dbase;
};

RowState computeRowState(int y, const float *ct, uint16_t rowLUT[ROWLUT_SIZE], int &lastBgIdx) {
    auto rowCos = [ct](float rad) { return ct[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; };
    auto rowSin = [&rowCos](float rad) { return rowCos(rad - 1.5707963f); };

    const float yz = static_cast<float>(y * g_zoom);
    const float warp1 = rowSin(yz * 0.021f + g_warpPh1) * g_A1;
    const float warp2 = rowSin(yz * 0.013f - g_warpPh2 + 1.7f) * g_A2;
    const float yn = y * g_invH; // was a divide (__divsf3 libcall on device)
    // Height (p3) and Spread (p4). At their defaults these two are exactly
    // 0.32f and 1.0f/0.85f, the constants that used to be written here.
    float env = 1.0f - fabsf(yn - g_envCenter) * g_envInv;
    env = env < 0 ? 0 : env * env;
    const int32_t envQ12 = static_cast<int32_t>(env * 4096.0f);
    RowState st;
    st.rowScale = (envQ12 * g_inten14) >> 12;

    const int bgIdx = static_cast<int>(yn * 10.0f); // sky sits in the darkest ~4% of the theme
    if (bgIdx != lastBgIdx) {
        buildRowLUT(rowLUT, bgIdx);
        lastBgIdx = bgIdx;
    }

    st.ph1 = g_phBase1 + static_cast<uint32_t>(static_cast<int32_t>(warp1 * TICKS));
    st.ph2 = g_phBase2 + static_cast<uint32_t>(static_cast<int32_t>(warp2 * TICKS));
    if (g_zoom == 2) {
        // The zoom-2 tables hold sin(2*theta) and repeat every 512 entries,
        // so angle theta is read at index theta / 2. A full turn is 2^18 Q8
        // ticks and 2^18 divides 2^32, so halving the wrapped uint32 is
        // exact modulo the 512-entry period. The kernel's STEP per column
        // then advances the angle by 2*STEP.
        st.ph1 >>= 1;
        st.ph2 >>= 1;
    }
    // Dither row phase keyed by the ROW PAIR, y >> 1, not by y itself.
    // Callers always pass ySrc (always even) as y here (see the 2026-09-05
    // redesign note in the file header), so this is really the pair's
    // phase, identical for both rows whether the second one is filled by
    // memcpy or by its own call to this same function. Zero extra cost
    // either way: same multiply-and-mask, just of a shifted y.
    st.dbase = ((y >> 1) & 7) * 8;
    return st;
}

// The portable C++ spec: same coarse-column-grid algorithm as
// auroraPixelsAsm() below (see the file header's perf-pass-4 note for the
// derivation and error bound). Kept independent of the kernel's own code so
// the device equivalence test (/api/debug/animtest) is checking two
// separately-written implementations of the same math, not one path calling
// the other.
// The last pixel of an odd-width row (233 px, the 466 panel's half
// resolution), which no pair covers (gm-bzu.49). It is the grid's next
// sample, at x = w - 1: ph1 and ph2 advanced by (w - 1) steps from the
// row's start, clamped, squared and row-scaled like every other sample,
// with column w - 1's dither. The pair loops leave scur at exactly this
// sample; recomputing it here lets the asm path, whose kernel keeps scur
// in a register, share one tail with bandRef.
void auroraOddTail(uint16_t *__restrict row, uint32_t ph1, uint32_t ph2, int32_t rowScale, const int32_t *rowDf,
                   const int32_t *__restrict w1, const int32_t *__restrict w2, const uint16_t *rowLUT, int w) {
    if ((w & 1) == 0) {
        return;
    }
    const uint32_t steps = static_cast<uint32_t>(w - 1);
    const uint32_t p1 = ph1 + steps * STEP1;
    const uint32_t p2 = ph2 + steps * STEP2;
    const int32_t v = w1[(p1 >> 8) & 1023] + w2[(p2 >> 8) & 1023];
    const int32_t vc = v > 0 ? v : 0;
    const int32_t s = (((vc * vc) >> 12) * rowScale) >> 12;
    row[w - 1] = rowLUT[s + rowDf[(w - 1) & 7]];
}

// Renders one row's full pixel content: computeRowState plus the coarse-
// column-grid pair loop, unchanged per-pixel math. Takes ySrc, not the row
// being written, because the caller always derives ySrc from y before
// deciding whether to duplicate (see bandRef below and the file header's
// 2026-09-05 note).
void renderAuroraRowRef(uint16_t *__restrict row, int ySrc, const int32_t *__restrict w1,
                         const int32_t *__restrict w2, const float *__restrict ct, uint16_t rowLUT[ROWLUT_SIZE],
                         int &lastBgIdx, const int32_t *ditherFold, int w) {
    const RowState st = computeRowState(ySrc, ct, rowLUT, lastBgIdx);
    uint32_t ph1 = st.ph1, ph2 = st.ph2;
    const int32_t rowScale = st.rowScale;
    const int32_t *rowDf = ditherFold + st.dbase;

    // One sample: the two-curtain sum at the CURRENT ph1/ph2, clamped,
    // squared and row-scaled down to a rowLUT index. Does not advance
    // ph1/ph2 or touch dither, the caller owns both.
    auto sampleScaledSq = [&]() -> int32_t {
        const int32_t v = w1[(ph1 >> 8) & 1023] + w2[(ph2 >> 8) & 1023];
        const int32_t vc = v > 0 ? v : 0;
        return (((vc * vc) >> 12) * rowScale) >> 12;
    };

    // Coarse column grid, spacing 2: sample the field (and its downstream
    // clamp/square/scale) at x=0,2,4,... and linearly interpolate the
    // ROWLUT INDEX for the odd pixel in between. An odd w leaves the last
    // pixel outside every pair; auroraOddTail after the loop writes it.
    const uint32_t ph1Row = ph1, ph2Row = ph2;
    int32_t scur = sampleScaledSq();
    for (int x = 0; x + 2 <= w; x += 2) {
        ph1 += STEP1;
        ph1 += STEP1; // advance 2 pixels' worth to reach the next sample
        ph2 += STEP2;
        ph2 += STEP2;
        const int32_t snext = sampleScaledSq();
        row[x] = rowLUT[scur + rowDf[x & 7]];
        // scur, snext are both >=0 (post-clamp), so a plain logical shift
        // is an exact average, no sign handling needed.
        const int32_t sodd = (scur + snext) >> 1;
        row[x + 1] = rowLUT[sodd + rowDf[(x + 1) & 7]];
        scur = snext;
    }
    auroraOddTail(row, ph1Row, ph2Row, rowScale, rowDf, w1, w2, rowLUT, w);
}

// A row's content and dither phase are always derived from ySrc = y & ~1
// (its pair's even row), never from y directly and never from anything
// about the call other than y itself. That is what makes this safe under
// SleepAnimation.cpp's row-level interlace path, which calls band() with
// rows==1 for one row at a time: a lone row computes ySrc's content
// directly and gets exactly the pixels it would have gotten as the
// duplicate half of a same-call pair, just without that call's memcpy
// saving. Production's usual call is rows==2 with y0 even (BAND_H), so the
// common case is exactly one compute-and-duplicate pair per call. See the
// file header's 2026-09-05 note for why this matters.
void bandRef(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const int32_t *__restrict w1 = wLut1;
    const int32_t *__restrict w2 = wLut2;
    const float *__restrict ct = cosTableF();

    uint16_t *const rowLUT = g_rowLUT; // cached across calls, see its declaration
    int &lastBgIdx = g_lastBgIdx;
    int32_t ditherFold[64];
    buildDitherFold(ditherFold);

    int row = 0;
    while (row < rows) {
        const int y = y0 + row;
        const int ySrc = y & ~1;
        uint16_t *out = dst + static_cast<size_t>(row) * w;
        if ((y & 1) == 0 && row + 1 < rows) {
            // y starts a pair and y+1 is also in this call: compute once,
            // duplicate. The common case for every real caller.
            renderAuroraRowRef(out, ySrc, w1, w2, ct, rowLUT, lastBgIdx, ditherFold, w);
            memcpy(out + w, out, static_cast<size_t>(w) * sizeof(uint16_t));
            row += 2;
        } else {
            // y is odd (its partner is the row behind it, not in this
            // call) or y is even but the call ends before y+1 (row-level
            // interlace). Either way, render ySrc's content directly so
            // this row matches what it would be as half of a same-call
            // pair.
            renderAuroraRowRef(out, ySrc, w1, w2, ct, rowLUT, lastBgIdx, ditherFold, w);
            row += 1;
        }
    }
}

#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)

// Same coarse-column-grid algorithm as bandRef's sampleScaledSq/pair loop
// above, as hand-scheduled scalar Xtensa. Still no PIE: the remaining
// per-pixel work is table gathers (two independent sine reads at each
// sample, one dependent color read at every pixel) and PIE has no vector
// gather on this chip, so nothing here vectorises -- see perf pass 3's
// note in the file header for why this stays scalar.
//
// One call processes `pairs` = w/2 pixel-pairs. scur0 is the caller's
// bootstrap sample (bandRef's `scaledSq at x=0`, computed once in band()
// below with the row's UNADVANCED ph1/ph2) so the kernel itself never needs
// bootstrap instructions before its loop -- every iteration has the same
// shape.
//
// Per pair: gather+advance+clamp+square+scale for the NEXT sample (SN,
// register t1) while the CURRENT sample (scur, persistent across
// iterations) is still valid; the even pixel's color is stored to memory
// AS SOON AS it is ready rather than held until both colors are packed
// into one write. Round 3 rescheduled this against
// tools/animbench/xtensa-asm14's compiled bandRef (GCC 14 -O2 on the
// identical C++ algorithm, same file): rung 4 showed this kernel LOSING to
// bandRef (0.92x in the back-to-back device harness) despite being
// bit-exact and spill-free, which fleet finding #2 says means it was
// scheduled worse, not doing more real work. Reading bandRef's compiled
// loop in xtensa-asm14/AnimAurora.S found two concrete things GCC did that
// round 2's kernel did not:
//   1. GCC clamps with a single MAX instruction (`max a8,a8,a9` against a
//      zero register) instead of the branchless srai/and/sub sequence this
//      kernel used -- 1 instruction instead of 3, and a strictly shorter
//      dependency chain feeding the first mull (this chip's Xtensa LX7
//      config includes MAX/MIN as a base ALU op; GCC only emits it because
//      the target supports it, so it is safe to write by hand here too).
//   2. GCC stores the two pixels as two independent s16i instructions
//      instead of packing them into one s32i. Packing (this kernel's round
//      2 approach: slli+or+s32i) is one instruction MORE, not fewer, and
//      forces the store to wait on a slli/or chain that depends on BOTH
//      colors; two plain s16i stores need no such join, and since they
//      write different addresses they don't even need to happen at the
//      same POINT in the instruction stream -- the even store below runs
//      well before the odd color is computed, freeing its register early.
// A first attempt also copied a third GCC habit -- batching both df loads
// and both rowLUT loads adjacently before either store -- but that needs 4
// scratch registers (15 operands total) and xtensa-asm14 rejected it
// outright, the same hard "impossible constraints" failure round 2's
// 16-operand attempt hit, not a soft spill: 15 is not safely between 14
// (works) and 16 (rejected), it is itself over the wall. This version gets
// the same effect a cheaper way: it still computes the df base address
// ONCE and reads both entries off it (offset 0 for even, offset 4 for
// odd), but loads df[bit_odd] only after the even color has already been
// computed AND STORED, so the even color's register is free again by the
// time the odd path needs one -- 3 scratch registers throughout, matching
// round 1/2's proven-safe budget.
// Both changes together drop this kernel from 38 to 34 instructions per
// pair (19 to 17/pixel) and, unlike round 2's version, actually fill both
// mull latency slots with independent work (round 2's kernel scheduled the
// df lookup entirely AFTER both mulls, hiding neither -- an oversight this
// pass fixes, not a deliberate round-2 choice).
//
// Register budget: dst, p1, p2, dfi, scur (5, loop-carried) + w1, w2, rl,
// df, rs, n (6, read-only) + t1, t2, t3 (3, scratch) = 14, matching round
// 1/2's proven-safe ceiling. Checked in xtensa-asm14/AnimAurora.S for
// spill code before calling this done (see this pass's report).
__attribute__((noinline)) static void auroraPixelsAsm(uint16_t *__restrict dst, const int32_t *__restrict w1,
                                                        const int32_t *__restrict w2,
                                                        const uint16_t *__restrict rowLUT,
                                                        const int32_t *__restrict df, uint32_t ph1, uint32_t ph2,
                                                        int32_t rowScale, int32_t scur0, int pairs) {
    uint32_t p1 = ph1, p2 = ph2;
    int32_t dfi = 0; // byte offset into df's 8 int32 entries (0/8/16/24), wraps via extui below
    int32_t scur = scur0;
    int32_t t1, t2, t3;
    // A production kernel must never write CPENABLE itself: the lazy
    // FreeRTOS coprocessor-enable is what keeps another task's FPU state
    // intact, and this kernel does not touch the FPU or PIE at all, so
    // there is nothing here that would need it.
    asm volatile(
        "loopnez %[n], 2f\n"
        // --- advance p1/p2 by 2*STEP FIRST, so the gather below lands 2
        // pixels ahead of the sample scur already holds (see round 2's
        // note in this comment's earlier revision, preserved in git
        // history, on why advance must precede gather) ---
        "addmi   %[p1], %[p1], %[ds1hi]\n" // ph1 += 2*STEP1, immediate-only, no register
        "addi    %[p1], %[p1], %[ds1lo]\n"
        "addmi   %[p2], %[p2], %[ds2hi]\n" // ph2 += 2*STEP2
        "addi    %[p2], %[p2], %[ds2lo]\n"
        // --- gather v = w1[idx1] + w2[idx2] at the now-advanced sample ---
        "extui   %[t1], %[p1], 8, 10\n" // idx1 = (ph1>>8)&1023
        "addx4   %[t1], %[t1], %[w1]\n"
        "l32i    %[t1], %[t1], 0\n" // t1 = w1[idx1]
        "extui   %[t2], %[p2], 8, 10\n"
        "addx4   %[t2], %[t2], %[w2]\n"
        "l32i    %[t2], %[t2], 0\n" // t2 = w2[idx2]
        // vc = max(v, 0): a native MAX against a zeroed register, not a
        // branchless srai/and/sub -- see the comment above for why. The
        // zero constant is materialized into t3 (idle at this point in the
        // iteration) BEFORE the add that consumes t2, instead of into t2
        // right after: t2 was just loaded (line above) and the add is its
        // very first consumer, which is exactly the one-cycle load-use
        // stall the device's interlock charges when the instruction right
        // after a load reads its result. Putting an independent instruction
        // (this movi, which was going to run anyway) between the load and
        // the add removes that stall for free; t2 is freed by the add and
        // is reused for the df base address two instructions later, same
        // as before. Measured on device (round 5, kb.py): see this
        // function's report entry for the min_ms delta.
        "movi    %[t3], 0\n"
        "add     %[t1], %[t1], %[t2]\n" // t1 = v (no longer the instruction right after t2's load)
        "max     %[t1], %[t1], %[t3]\n" // t1 = vc
        "mull    %[t1], %[t1], %[t1]\n" // vc*vc; 2-cycle latency, filled below
        "extui   %[t2], %[dfi], 0, 5\n" // df base address, part 1 (latency filler)
        "add     %[t2], %[df], %[t2]\n" // t2 = df base address (kept as an address, not consumed yet)
        "addi    %[dfi], %[dfi], 8\n"   // advance dfi for the next pair (persistent, no register cost)
        "srli    %[t1], %[t1], 12\n"    // sq
        "mull    %[t1], %[t1], %[rs]\n" // sq*rowScale; 2-cycle latency, filled below
        "l32i    %[t3], %[t2], 0\n"     // df[bit_even] (latency filler; t2 still holds the base address)
        "srli    %[t1], %[t1], 12\n"    // t1 = SN
        // --- t1=SN, t2=df base address (still valid), t3=df[bit_even],
        // scur=OLD sample ---
        "add     %[t3], %[scur], %[t3]\n" // t3 = even rowLUT index (raw)
        "addx2   %[t3], %[t3], %[rl]\n"
        "l16ui   %[t3], %[t3], 0\n" // t3 = color_even
        "s16i    %[t3], %[dst], 0\n" // store even now, freeing t3 immediately
        // --- t2 was never touched above, so it is still the SAME base
        // address; the odd entry is a plain +4-byte read off it, no
        // recomputed extui/add ---
        "l32i    %[t2], %[t2], 4\n" // t2 = df[bit_odd]
        // Round 5 tried moving this load up to sit between the color_even
        // load and its store (giving that store a filler, the same fix
        // applied to the v = w1+w2 gather above). It measured WORSE on the
        // device (min_ms 25.60-25.62 vs 25.44-25.45, consistent over two
        // repeats), not better as the same reasoning predicted for the
        // gather stall. Reverted. The device is the only authority here;
        // do not reapply this without a fresh measurement.
        "add     %[t3], %[scur], %[t1]\n" // t3 = scur_old + SN (raw sum, for interpolation)
        "srli    %[t3], %[t3], 1\n"       // t3 = SO
        "or      %[scur], %[t1], %[t1]\n" // scur <- SN now; every read of the OLD scur is done above
        "add     %[t1], %[t3], %[t2]\n"   // t1 = odd rowLUT index (raw) = SO + df[bit_odd]
        "addx2   %[t1], %[t1], %[rl]\n"
        "l16ui   %[t1], %[t1], 0\n" // t1 = color_odd
        "s16i    %[t1], %[dst], 2\n" // store odd
        "addi    %[dst], %[dst], 4\n"
        "2:\n"
        : [dst] "+r"(dst), [p1] "+r"(p1), [p2] "+r"(p2), [dfi] "+r"(dfi), [scur] "+r"(scur), [t1] "=&r"(t1),
          [t2] "=&r"(t2), [t3] "=&r"(t3)
        : [w1] "r"(w1), [w2] "r"(w2), [rl] "r"(rowLUT), [df] "r"(df), [rs] "r"(rowScale), [n] "r"(pairs),
          [ds1hi] "i"(DSTEP1_HI), [ds1lo] "i"(DSTEP1_LO), [ds2hi] "i"(DSTEP2_HI), [ds2lo] "i"(DSTEP2_LO)
        : "memory");
}

// Computes ySrc's row state and runs the asm kernel into one row. Same
// ySrc-derivation contract as renderAuroraRowRef above.
void renderAuroraRowAsm(uint16_t *row, int ySrc, const int32_t *__restrict w1,
                         const int32_t *__restrict w2, const float *__restrict ct, uint16_t rowLUT[ROWLUT_SIZE],
                         int &lastBgIdx, const int32_t *ditherFold, int w) {
    const RowState st = computeRowState(ySrc, ct, rowLUT, lastBgIdx);

    // Bootstrap sample at x=0, using the row's unadvanced ph1/ph2, matches
    // renderAuroraRowRef's `scur = sampleScaledSq()` before its pair loop.
    const int32_t v0 = w1[(st.ph1 >> 8) & 1023] + w2[(st.ph2 >> 8) & 1023];
    const int32_t vc0 = v0 > 0 ? v0 : 0;
    const int32_t scur0 = (((vc0 * vc0) >> 12) * st.rowScale) >> 12;
    auroraPixelsAsm(row, w1, w2, rowLUT, ditherFold + st.dbase, st.ph1, st.ph2, st.rowScale, scur0, w >> 1);
    auroraOddTail(row, st.ph1, st.ph2, st.rowScale, ditherFold + st.dbase, w1, w2, rowLUT, w);
}

// Device path. Same ySrc/row-stride structure as bandRef above, so the two
// stay pixel-for-pixel identical to each other. The kernel call always
// renders ySrc's content; the memcpy only fires when y's pair partner is
// also in this call (see bandRef's comment for why this is call-shape
// safe). No GM_ANIM_IRAM this round: see the file header's perf-pass-4 note
// for why it was dropped.
void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const int32_t *__restrict w1 = wLut1;
    const int32_t *__restrict w2 = wLut2;
    const float *__restrict ct = cosTableF();

    uint16_t *const rowLUT = g_rowLUT; // cached across calls, see its declaration
    int &lastBgIdx = g_lastBgIdx;
    int32_t ditherFold[64];
    buildDitherFold(ditherFold);

    int row = 0;
    while (row < rows) {
        const int y = y0 + row;
        const int ySrc = y & ~1;
        uint16_t *out = dst + static_cast<size_t>(row) * w;
        if ((y & 1) == 0 && row + 1 < rows) {
            renderAuroraRowAsm(out, ySrc, w1, w2, ct, rowLUT, lastBgIdx, ditherFold, w);
            memcpy(out + w, out, static_cast<size_t>(w) * sizeof(uint16_t));
            row += 2;
        } else {
            renderAuroraRowAsm(out, ySrc, w1, w2, ct, rowLUT, lastBgIdx, ditherFold, w);
            row += 1;
        }
    }
}

#else

void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) { bandRef(dst, y0, rows, w, tMs, p); }

#endif // __XTENSA__ && !GM_BGANIM_NO_ASM

void release() {
    releaseTable(glowLUT, 256 * sizeof(uint16_t));
    releaseTable(wLut1, static_cast<size_t>(SIN_N) * sizeof(int32_t));
    releaseTable(wLut2, static_cast<size_t>(SIN_N) * sizeof(int32_t));
    releaseTable(g_rowLUT, static_cast<size_t>(ROWLUT_SIZE) * sizeof(uint16_t));
    g_lastBgIdx = -1;
    lastThemeGen = 0xFFFFFFFF;
    // Same reason as lastThemeGen: the next init() builds glowLUT with
    // whatever gain the last run left behind, so the first frame after it
    // has to see a value it cannot match and rebuild.
    g_glowGain = 2.2f;
    g_glowP = 0xFF;
}

} // namespace

extern const BgAnimation bg_anim_aurora;
const BgAnimation bg_anim_aurora = {
    "aurora",
    "Aurora",
    {{"speed", "Speed", 50},
     {"intensity", "Intensity", 55},
     {"waviness", "Waviness", 50},
     {"height", "Height", 50},
     {"spread", "Spread", 50},
     {"glow", "Glow", 50},
     {"drift", "Drift", 50},
     {nullptr, nullptr, 0}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
