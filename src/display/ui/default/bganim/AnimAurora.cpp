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
//     fit the slab with 512 B to spare and no shrinking was needed. Before
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
#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

namespace {
using namespace bganim;

uint16_t *glowLUT = nullptr; // [256 intensity] -> RGB565 glow color (theme-baked)
uint32_t lastThemeGen = 0xFFFFFFFF;

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

float g_t = 0, g_A1 = 0, g_A2 = 0;
int32_t g_inten14 = 0; // intensity * 1.4 in Q8

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

// Curtain color rides the theme's mid-to-bright range; the fade ramp keeps
// low intensities near-black so the additive blend stays subtle.
void buildGlowLUT() {
    for (int i = 0; i < 256; i++) {
        uint8_t c[3];
        themeRGB(40 + ((i * 215) >> 8), c);
        const float scale = fminf(1.0f, (i / 255.0f) * 2.2f);
        glowLUT[i] = rgb565(clamp8f(c[0] * scale), clamp8f(c[1] * scale), clamp8f(c[2] * scale));
    }
}

bool init(int, int) {
    const int16_t *lut = sinLut();
    if (lut == nullptr) {
        return false;
    }
    // Round 2: allocHot(), not alloc(). All three tables are read every
    // pixel or every row (glowLUT indirectly, through rowLUT which is
    // rebuilt from it up to ~10x/frame), which is exactly the "per-pixel or
    // per-row" criterion BgAnimCommon.h's placement comment names for the
    // hot slab. Total 8,704 B (4,096 + 4,096 + 512), all 16-byte-aligned
    // already, against the slab's 9,216 B per-animation share -- fits with
    // 512 B to spare, no shrinking needed. alloc() is PSRAM unconditionally
    // as of this round, so leaving these on alloc() would have made every
    // boot pay the 46.7 -> 67.6 ms PSRAM penalty this file's header
    // measured, not just some boots.
    if (glowLUT == nullptr) {
        glowLUT = static_cast<uint16_t *>(allocHot(256 * sizeof(uint16_t)));
    }
    if (wLut1 == nullptr) {
        wLut1 = static_cast<int32_t *>(allocHot(SIN_N * sizeof(int32_t)));
    }
    if (wLut2 == nullptr) {
        wLut2 = static_cast<int32_t *>(allocHot(SIN_N * sizeof(int32_t)));
    }
    if (glowLUT == nullptr || wLut1 == nullptr || wLut2 == nullptr) {
        return false;
    }
    for (int i = 0; i < SIN_N; i++) {
        wLut1[i] = (static_cast<int32_t>(lut[i]) * W1) >> 7;
        wLut2[i] = (static_cast<int32_t>(lut[i]) * W2) >> 7;
    }
    buildGlowLUT();
    lastThemeGen = themeGen();
    return true;
}

void frame(uint32_t tMs, int, int, const uint8_t p[4]) {
    if (themeGen() != lastThemeGen) {
        buildGlowLUT();
        lastThemeGen = themeGen();
    }
    g_t = (tMs * 0.001f) * 0.45f * speedMul(p[0]);
    g_A1 = 0.6f + (p[2] / 100.0f) * 2.4f;
    g_A2 = 0.4f + (p[2] / 100.0f) * 1.6f;
    g_inten14 = static_cast<int32_t>((p[1] / 100.0f) * 1.4f * 256.0f);
    // Wraparound-safe once/frame (see note by g_phBase1/2 above); replaces the
    // 960x/frame int64 conversion that used to run per-row inside band().
    g_phBase1 = static_cast<uint32_t>(static_cast<int64_t>(g_t * 0.12f * TICKS));
    g_phBase2 = static_cast<uint32_t>(static_cast<int64_t>(g_t * 0.07f * TICKS));
}

// Pure helpers shared by both bandRef and the asm dispatch path below (both
// need identical row-constant tables; these are the only piece it is safe
// to share, since they take no hidden state beyond their arguments).
void buildDitherFold(int32_t out[64]) {
    for (int i = 0; i < 64; i++) {
        out[i] = ROWLUT_PAD + ((static_cast<int32_t>(BAYER8[i]) - 32) >> 2);
    }
}

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

RowState computeRowState(int y, float t, const float *ct, uint16_t rowLUT[ROWLUT_SIZE], int &lastBgIdx) {
    auto rowCos = [ct](float rad) { return ct[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; };
    auto rowSin = [&rowCos](float rad) { return rowCos(rad - 1.5707963f); };

    const float warp1 = rowSin(y * 0.021f + t * 0.5f) * g_A1;
    const float warp2 = rowSin(y * 0.013f - t * 0.44f + 1.7f) * g_A2;
    const float yn = y * (1.0f / 480.0f); // was a divide (__divsf3 libcall on device)
    float env = 1.0f - fabsf(yn - 0.32f) * (1.0f / 0.85f);
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
    st.dbase = (y & 7) * 8;
    return st;
}

// The portable C++ spec: same coarse-column-grid algorithm as
// auroraPixelsAsm() below (see the file header's perf-pass-4 note for the
// derivation and error bound). Kept independent of the kernel's own code so
// the device equivalence test (/api/debug/animtest) is checking two
// separately-written implementations of the same math, not one path calling
// the other.
void bandRef(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const float t = g_t;
    const int32_t *__restrict w1 = wLut1;
    const int32_t *__restrict w2 = wLut2;
    const float *__restrict ct = cosTableF();

    uint16_t rowLUT[ROWLUT_SIZE];
    int lastBgIdx = -1;
    int32_t ditherFold[64];
    buildDitherFold(ditherFold);

    for (int ry = 0; ry < rows; ry++) {
        const int y = y0 + ry;
        const RowState st = computeRowState(y, t, ct, rowLUT, lastBgIdx);
        uint32_t ph1 = st.ph1, ph2 = st.ph2;
        const int32_t rowScale = st.rowScale;
        const int32_t *rowDf = ditherFold + st.dbase;
        uint16_t *__restrict row = dst + static_cast<size_t>(ry) * w;

        // One sample: the two-curtain sum at the CURRENT ph1/ph2, clamped,
        // squared and row-scaled down to a rowLUT index. Does not advance
        // ph1/ph2 or touch dither -- the caller owns both.
        auto sampleScaledSq = [&]() -> int32_t {
            const int32_t v = w1[(ph1 >> 8) & 1023] + w2[(ph2 >> 8) & 1023];
            const int32_t vc = v > 0 ? v : 0;
            return (((vc * vc) >> 12) * rowScale) >> 12;
        };

        // Coarse column grid, spacing 2: sample the field (and its
        // downstream clamp/square/scale) at x=0,2,4,...  and linearly
        // interpolate the ROWLUT INDEX for the odd pixel in between. w is
        // always even (480 or 240), so every pixel is covered by exactly
        // one pair, no remainder loop.
        int32_t scur = sampleScaledSq();
        for (int x = 0; x + 2 <= w; x += 2) {
            ph1 += STEP1;
            ph1 += STEP1; // advance 2 pixels' worth to reach the next sample
            ph2 += STEP2;
            ph2 += STEP2;
            const int32_t snext = sampleScaledSq();
            row[x] = rowLUT[scur + rowDf[x & 7]];
            // scur, snext are both >=0 (post-clamp), so a plain logical
            // shift is an exact average -- no sign handling needed.
            const int32_t sodd = (scur + snext) >> 1;
            row[x + 1] = rowLUT[sodd + rowDf[(x + 1) & 7]];
            scur = snext;
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
        "add     %[t1], %[t1], %[t2]\n" // t1 = v
        // vc = max(v, 0): a native MAX against a zeroed register, not a
        // branchless srai/and/sub -- see the comment above for why.
        "movi    %[t2], 0\n"
        "max     %[t1], %[t1], %[t2]\n" // t1 = vc
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

// Device path: same row-constant math as bandRef (computeRowState is
// shared; only the x loop after it differs -- a compiled C++ walk there,
// one auroraPixelsAsm() call here). No GM_ANIM_IRAM this round: see the
// file header's perf-pass-4 note for why it was dropped.
void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const float t = g_t;
    const int32_t *__restrict w1 = wLut1;
    const int32_t *__restrict w2 = wLut2;
    const float *__restrict ct = cosTableF();

    uint16_t rowLUT[ROWLUT_SIZE];
    int lastBgIdx = -1;
    int32_t ditherFold[64];
    buildDitherFold(ditherFold);

    for (int ry = 0; ry < rows; ry++) {
        const int y = y0 + ry;
        const RowState st = computeRowState(y, t, ct, rowLUT, lastBgIdx);

        // Bootstrap sample at x=0, using the row's unadvanced ph1/ph2 --
        // matches bandRef's `scur = sampleScaledSq()` before its pair loop.
        const int32_t v0 = w1[(st.ph1 >> 8) & 1023] + w2[(st.ph2 >> 8) & 1023];
        const int32_t vc0 = v0 > 0 ? v0 : 0;
        const int32_t scur0 = (((vc0 * vc0) >> 12) * st.rowScale) >> 12;

        uint16_t *row = dst + static_cast<size_t>(ry) * w;
        auroraPixelsAsm(row, w1, w2, rowLUT, ditherFold + st.dbase, st.ph1, st.ph2, st.rowScale, scur0, w >> 1);
    }
}

#else

void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) { bandRef(dst, y0, rows, w, tMs, p); }

#endif // __XTENSA__ && !GM_BGANIM_NO_ASM

void release() {
    releaseTable(glowLUT, 256 * sizeof(uint16_t));
    releaseTable(wLut1, static_cast<size_t>(SIN_N) * sizeof(int32_t));
    releaseTable(wLut2, static_cast<size_t>(SIN_N) * sizeof(int32_t));
    lastThemeGen = 0xFFFFFFFF;
}

} // namespace

extern const BgAnimation bg_anim_aurora;
const BgAnimation bg_anim_aurora = {
    "aurora",
    "Aurora",
    {{"speed", "Speed", 50}, {"intensity", "Intensity", 55}, {"waviness", "Waviness", 50}, {nullptr, nullptr, 0}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
