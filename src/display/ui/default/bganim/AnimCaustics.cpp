#ifndef GAGGIMATE_SIM

// "Caustics" — three plane waves at slowly rotating angles sum into drifting
// light filaments (threshold + square) on deep blue-black water.
//
// Optimized (opt-caustics, 2026-08-15): the original evaluated three float
// LUT sines per pixel plus float threshold/square/palette shaping — the
// worst band() cost in the fleet. Techniques applied:
//
// 1. Coarse-grid sampling. The interference field (sum of three waves) is
//    smooth relative to a few pixels, so each wave's phase is only evaluated
//    exactly every GRID=4 columns; the raw integer sum is linearly
//    interpolated across the span (one subtract+shift per span, one integer
//    add per pixel — no per-pixel trig or division). The nonlinear
//    threshold/square shaping still runs per exact pixel (via shapeLUT
//    below), so only the smooth wave-sum itself is approximated.
// 2. Fixed-point DDS phase accumulators (uint32_t, full circle = 2^32,
//    matching the scheme AnimEmber already uses) index bganim::sin1024's
//    backing LUT directly via `>> 22` — zero float per pixel, zero libm
//    anywhere in band().
// 3. bganim::sinLut() itself is NOT inlined (lazy-init guard in a different
//    translation unit) — the xtensa-asm tool showed every sin1024() call
//    compiling to a real device CALL, which also disqualifies the loop from
//    GCC's zero-overhead LOOP instruction. Fixed by fetching the LUT pointer
//    once per band() call and indexing it directly thereafter.
// 4. Nonlinear shaping (abs, threshold, invSpan, square, *255) depends only
//    on the frame-constant thresh/invSpan and the pixel's wave-sum magnitude
//    (0..3*512), so it's baked into a per-frame shapeLUT — one lookup
//    replaces several float ops. The table is over-sized past the true max
//    so interpolation overshoot needs no clamp branch.
// 5. Dither-baked RGB565 palette buckets. There are only 16 distinct dither
//    values (4x4 Bayer), so instead of adding dither to each of R/G/B and
//    clamping/packing per pixel, 16 full 256-entry RGB565 tables (dither
//    already added, clamped, and packed) are rebuilt once per frame. Per
//    pixel this turns "3 adds + 3 clamps + rgb565 pack" into a single
//    uint16_t lookup indexed by the shading index.
// 6. Branchless abs (shift/xor/sub) removes the remaining per-pixel branch.
// Design: anim-water (Fable), 2026-08-15.
//
// Optimized (asm-caustics, 2026-09-04): band() dropped to a hand-written
// Xtensa kernel on device; the portable version above (unchanged math) now
// lives in bandRef, the spec the host bench and the on-device band() vs
// bandRef() equivalence test both check against. Two things changed to make
// that kernel fit the register file, and BOTH sides (bandRef and the kernel)
// were moved to them together so the two stay pixel-exact by construction,
// not by coincidence:
//
// - rgbLUT's layout moved from [ditherIndex][shading] (16 x 256, one base
//   pointer per column phase) to [rowPhase][shading][colSlot] (4 x 256 x 4
//   at the time, stride 4; the shading dimension later shrank to
//   SHADE_LEVELS=128 in round 2 below, so the byte counts and the *1024
//   stride in this paragraph are the round-1 numbers, kept for the reasoning
//   trail -- the live code uses ROWPHASE_STRIDE). Same 4096 entries, same
//   8192 bytes at the time, only the storage order changed. The reason is
//   register count, not speed: the original layout needs FOUR live pointer
//   registers (rgb0..rgb3, one per x&3 bucket) for the whole row;
//   xtensa-asm14/AnimCaustics.S (device compiler) showed GCC could not keep
//   even that layout's pointers resident and was reloading three of them
//   from the stack every 4-pixel span (six `l32i ..., sp, ...` per iteration
//   total, counting the phase-step spills alongside them) - see
//   causticsRowKernel's own comment for the exact count. The new layout
//   needs only ONE pointer (rgbRowBase = rgbLUT + rowPhase*ROWPHASE_STRIDE):
//   `addx8` computes rgbRowBase + shading*8 (four uint16_t per shading
//   level), and `l16ui`'s immediate offset (0/2/4/6, all in range) reaches
//   the pixel's own colSlot for free. That's the register the hand kernel's
//   14-operand budget (see its comment) could not otherwise have spared.
// - band()/bandRef()'s per-row setup (phaseQ init, the fresh sumCur gather
//   at x=0, the rgbLUT row-phase base pointer) is factored into
//   computeRowSetup(), called from both, so the two paths cannot drift by
//   one of them updating its copy of this arithmetic and not the other.
//
// The kernel itself is scalar, not PIE: the two per-pixel lookups
// (shapeLUT, then the palette) are data-dependent gathers and PIE has no
// gather instruction (ASM_BRIEF.md, "Where PIE does not fit"). See
// causticsRowKernel's comment for the register budget, the instruction
// count, and where the load-use stalls that budget could not schedule away
// are paid.
//
// Round 2 (asm-caustics, 2026-09-04): device measurement (production
// firmware, real placement, not the host bench) showed table placement
// dominates: this animation ran 22.6 ms/frame with its tables in internal
// SRAM and 37.5 ms in PSRAM, a bigger swing than anything left to win in the
// kernel. Two changes:
//
// - Both tables now come from bganim::allocHot() (the fixed internal-DRAM
//   slab, BgAnimCommon.h) instead of alloc() (PSRAM, unconditionally as of
//   this pass): they are each read once per pixel, exactly the criterion the
//   slab exists for, and unlike the old init()-time "does the free pool have
//   room" placement, this is a fixed cost that does not flip between SRAM
//   and PSRAM from one boot to the next.
// - rgbLUT's shading resolution moved from 256 to SHADE_LEVELS=128 (see its
//   own comment): the original 9,744 B combined footprint (rgbLUT 8,192 +
//   shapeLUT 1,552) does not fit this animation's 9,216 B share of the slab,
//   and rgbLUT is the one worth cutting since it is 5.3x shapeLUT's size for
//   the same one-read-per-pixel frequency. Halving it to 4,096 B brings the
//   pair to 5,648 B, comfortably inside budget, and shrinks the working set
//   the per-pixel gather touches regardless of where it ends up placed --
//   the same cut that helps it fit the slab also helps it if it ever does
//   not (a fallback to alloc()/PSRAM now misses less often per row, since
//   four rowPhase planes of 512 B each cycle instead of 2,048 B each).
//
// causticsRowKernel needed NO change for this: the palette stride it
// addresses (addx8, 4 colSlot entries x 2 bytes = 8 bytes per shading level)
// is set by DITHER_N's column-dither count, not by SHADE_LEVELS, so shrinking
// the shading dimension changes only how many 8-byte steps exist per
// rowPhase plane (ROWPHASE_STRIDE), which is a value the kernel already took
// as a plain argument (rgbRowBase) rather than a compile-time constant.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

namespace {
using namespace bganim;

constexpr int K = 3;
constexpr float BASE_ANGLE[K] = {0.35f, 2.55f, 4.55f};
constexpr float ANG_DRIFT[K] = {0.014f, -0.010f, 0.007f};
constexpr float PHASE0[K] = {0.0f, 2.1f, 4.6f};
constexpr float FREQ_BASE[K] = {0.046f, 0.061f, 0.037f};
constexpr float SPEED_MUL[K] = {1.0f, 0.82f, 1.28f};

// DDS phase scale: a uint32_t phase accumulator's full range (2^32) maps to
// one full circle (2*pi radians), same convention as AnimEmber's STEP1/2/3.
// idx = phaseQ >> 22 lands in [0,1023], directly indexing sin1024's LUT
// (SIN_N == 1024 == 2^(32-22)).
constexpr double PHASE_SCALE = 4294967296.0 / 6.283185307179586;
constexpr int PHASE_SHIFT = 22; // 32 - log2(SIN_N)

// Coarse-grid stride in x: the wave-sum field is evaluated exactly every
// GRID columns and linearly interpolated in between (see file header). Must
// be a power of two (interpolation step uses a shift, not a divide) and a
// multiple of 4 to keep the device kernel's 4-wide unroll exact.
constexpr int GRID = 4;

// |sum of 3 sin1024 outputs| ranges 0..K*SIN_AMP inclusive; shapeLUT maps
// that magnitude straight to a shading index (0..255), threshold+square
// baked in, rebuilt once per frame from thresh/invSpan. Sized with margin
// past SHAPE_MAX so integer-interpolation overshoot (bounded by GRID-1)
// never needs a clamp branch in band(); the formula naturally saturates
// there since normalized bright already clamps to 1.
constexpr int SHAPE_MAX = K * SIN_AMP; // 1536
constexpr int SHAPE_PAD = 16;          // > GRID-1 worst-case overshoot
constexpr int SHAPE_N = SHAPE_MAX + SHAPE_PAD;

// Only 16 distinct dither offsets exist (4x4 Bayer); rgbLUT holds a full
// dither-applied, clamped, RGB565-packed SHADE_LEVELS-entry palette per
// offset, so band() never adds/clamps/packs per pixel, just one lookup.
// Layout is [rowPhase][shading][colSlot] (rowPhase = y&3, colSlot = x&3),
// stride 4 -- see the file header for why this replaced the original
// [dither][shading] layout. ditherIndex = (rowPhase<<2)|colSlot, unchanged.
constexpr int DITHER_N = 16;

// Shading resolution rgbLUT stores, halved from the original 256 in the
// round-2 hot-slab pass (see file header): rgbLUT is the dominant table
// (8192 of the original 9744 bytes), so it's the one worth shrinking, and
// dithering already exists specifically to hide quantization steps -- it
// was tuned for the RGB565 packing's own levels, not for this axis, but a
// 128-step gradient plus the existing 4x4 dither still measured bit-exact
// against golden/ (see the report). shapeLUT's output range moves with it
// (frame()'s `bright * (SHADE_LEVELS - 1)`); its own input size (SHAPE_N,
// the wave-magnitude domain) is unrelated and unchanged.
constexpr int SHADE_LEVELS = 128;
constexpr int ROWPHASE_STRIDE = SHADE_LEVELS * 4; // bytes-as-uint16-entries per rowPhase plane

uint16_t *rgbLUT = nullptr; // [4][SHADE_LEVELS][4], flattened rowPhase*ROWPHASE_STRIDE+shading*4+colSlot
uint32_t lastThemeGen = 0xFFFFFFFF;

uint32_t g_rowFreqQ[K]; // per-row-unit phase step (y * this), DDS units
uint32_t g_phaseQ[K];   // phase intercept, DDS units
uint32_t g_stepQ[K];    // per-pixel (x) phase step, DDS units
int8_t ditherI[DITHER_N]; // (BAYER4[i]-7.5)*0.5, precomputed once at init
uint8_t *shapeLUT = nullptr;

void buildThemePalette() {
    // Sampled at SHADE_LEVELS positions spread across the full 0..255 theme
    // gradient (li*255/(SHADE_LEVELS-1), integer division -- this runs once
    // per frame/theme change, not per pixel, so the divide is free) rather
    // than at 256 and then discarding half: the color values under each of
    // the SHADE_LEVELS steps stay exactly as accurate as before, only the
    // number of steps along the ramp is fewer.
    uint8_t baseR[SHADE_LEVELS], baseG[SHADE_LEVELS], baseB[SHADE_LEVELS];
    for (int i = 0; i < SHADE_LEVELS; i++) {
        uint8_t c[3];
        themeRGB(i * 255 / (SHADE_LEVELS - 1), c);
        baseR[i] = c[0];
        baseG[i] = c[1];
        baseB[i] = c[2];
    }
    // rowPhase outer, colSlot middle, shading inner-strided: bucket[li*4]
    // writes the (rowPhase, colSlot) plane's shading-li entry directly at
    // its final stride-4 position, see the file header for why this
    // layout exists (one base pointer for the device kernel instead of
    // four).
    for (int rowPhase = 0; rowPhase < 4; rowPhase++) {
        for (int colSlot = 0; colSlot < 4; colSlot++) {
            const int di = (rowPhase << 2) | colSlot;
            const int d = ditherI[di];
            uint16_t *bucket = rgbLUT + rowPhase * ROWPHASE_STRIDE + colSlot;
            for (int li = 0; li < SHADE_LEVELS; li++) {
                int r = baseR[li] + d;
                int g = baseG[li] + d;
                int b = baseB[li] + d;
                r = r < 0 ? 0 : (r > 255 ? 255 : r);
                g = g < 0 ? 0 : (g > 255 ? 255 : g);
                b = b < 0 ? 0 : (b > 255 ? 255 : b);
                bucket[li * 4] = rgb565(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b));
            }
        }
    }
}

bool init(int, int) {
    // Hot slab (bganim::allocHot, BgAnimCommon.h): both tables are read once
    // per pixel (230,400 times/frame), which is exactly the "read per pixel"
    // criterion the slab exists for. Combined they are
    // DITHER_N*SHADE_LEVELS*2 + SHAPE_N = 4096 + 1552 = 5648 bytes, comfortably
    // under the 9,216 B this animation gets (see the SHADE_LEVELS comment for
    // where the other half of the original 9744 B went). A request that
    // doesn't fit falls back to PSRAM automatically (allocHot never returns
    // nullptr for that reason alone); release() gives both back through the
    // same releaseTable() calls as before.
    if (rgbLUT == nullptr) {
        rgbLUT = static_cast<uint16_t *>(allocHot(DITHER_N * SHADE_LEVELS * sizeof(uint16_t)));
    }
    if (shapeLUT == nullptr) {
        shapeLUT = static_cast<uint8_t *>(allocHot(SHAPE_N));
    }
    if (rgbLUT == nullptr || shapeLUT == nullptr) {
        return false;
    }
    for (int i = 0; i < DITHER_N; i++) {
        ditherI[i] = static_cast<int8_t>(lroundf((BAYER4[i] - 7.5f) * 0.5f));
    }
    buildThemePalette();
    lastThemeGen = themeGen();
    return true;
}

// The DDS phase words are deliberately modular — only the low 32 bits are
// ever used (band() masks with SIN_N-1 after shifting). But the values being
// converted leave the range of uint32_t almost immediately: the phase
// intercept grows without bound with uptime, and sinA*freq / cosA*freq are
// negative for most of the angle sweep. Converting a double that is negative
// or >= 2^32 directly to uint32_t is undefined behaviour, not a wrap: the
// platform may saturate instead. x86 happens to wrap, which is exactly why
// the host harness matched the reference frames while the real target's
// behaviour was never actually guaranteed — and why -fsanitize=undefined
// alone stayed silent here (GCC does not fold float-cast-overflow into it).
// Reduce into int64_t first, where the conversion is defined, and let the
// integer-to-unsigned conversion perform the modular wrap the design wants.
inline uint32_t ddsQ(double v) { return static_cast<uint32_t>(static_cast<int64_t>(fmod(v, 4294967296.0))); }

void frame(uint32_t tMs, int, int, const uint8_t p[4]) {
    if (themeGen() != lastThemeGen) {
        buildThemePalette();
        lastThemeGen = themeGen();
    }
    const float t = tMs * 0.001f;
    const float freqScale = lerpf(0.55f, 1.9f, p[1] / 100.0f);
    const float speedScale = 0.8f * speedMul(p[0]);
    const float thresh = 0.14f + 0.55f * (p[2] / 100.0f); // p[2] = "contrast" param
    const float invSpan = 1.0f / fmaxf(1e-3f, 1.0f - thresh);

    for (int k = 0; k < K; k++) {
        const float ang = BASE_ANGLE[k] + ANG_DRIFT[k] * t;
        const float cosA = fastCosRad(ang);
        const float sinA = fastSinRad(ang);
        const float freq = FREQ_BASE[k] * freqScale; // rad/pixel
        const float phaseRad = PHASE0[k] + t * speedScale * SPEED_MUL[k] * 2.0f;
        g_rowFreqQ[k] = ddsQ(static_cast<double>(sinA * freq) * PHASE_SCALE);
        g_phaseQ[k] = ddsQ(static_cast<double>(phaseRad) * PHASE_SCALE);
        g_stepQ[k] = ddsQ(static_cast<double>(cosA * freq) * PHASE_SCALE);
    }

    // Nonlinear shaping LUT: |sum of 3 sin1024 outputs| -> shading index
    // (0..SHADE_LEVELS-1). Rebuilt once per frame, libm/float here is free
    // per the brief (this is frame(), not the per-pixel band() loop).
    // Entries past SHAPE_MAX are still computed with the same formula; the
    // bright clamp (i*NORM can exceed 1) makes them saturate to the same
    // value shapeLUT[SHAPE_MAX] would hold, so band() never needs to clamp
    // mag.
    constexpr float NORM = 1.0f / static_cast<float>(SHAPE_MAX);
    for (int i = 0; i < SHAPE_N; i++) {
        float bright = (i * NORM - thresh) * invSpan;
        bright = bright < 0 ? 0 : (bright > 1 ? 1 : bright);
        bright *= bright;
        shapeLUT[i] = static_cast<uint8_t>(bright * static_cast<float>(SHADE_LEVELS - 1) + 0.5f);
    }
}

// Per-row state shared by bandRef and band()'s Xtensa dispatch: this wave's
// three phase intercepts at this row, the exact (non-interpolated) wave-sum
// at x=0, and the rgbLUT row-phase base pointer. Factored out so the two
// band paths cannot drift apart by one of them updating its own copy of this
// arithmetic and not the other.
struct RowSetup {
    uint32_t phase[K];
    int32_t sumCur0;
    const uint16_t *rowBase;
};

inline RowSetup computeRowSetup(int y, const int16_t *lut) {
    RowSetup rs;
    for (int k = 0; k < K; k++) {
        rs.phase[k] = static_cast<uint32_t>(y) * g_rowFreqQ[k] + g_phaseQ[k];
    }
    rs.sumCur0 = lut[(rs.phase[0] >> PHASE_SHIFT) & (SIN_N - 1)] + lut[(rs.phase[1] >> PHASE_SHIFT) & (SIN_N - 1)] +
                 lut[(rs.phase[2] >> PHASE_SHIFT) & (SIN_N - 1)];
    rs.rowBase = rgbLUT + static_cast<size_t>(y & 3) * ROWPHASE_STRIDE;
    return rs;
}

// Portable reference implementation, pixel-exact spec for band(): the host
// bench runs this against golden/, and it is what the on-device equivalence
// test (SleepAnimation::runAnimTest, /api/debug/animtest) compares band()'s
// output against. Unchanged math from the original band() (see file header
// "opt-caustics"); only the rgbLUT indexing moved to the [rowPhase][shading]
// [colSlot] layout (see the "asm-caustics" header section) via
// computeRowSetup()/rowBase*4+colSlot.
void bandRef(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    // Fetch the shared sine LUT pointer once: bganim::sin1024() is inline
    // but its backing bganim::sinLut() is a plain (non-inline) function
    // with a lazy-init guard in another translation unit, so every call
    // compiles to a real device CALL — which also blocks GCC's
    // zero-overhead LOOP instruction. Cache the pointer and index it
    // directly instead of calling sin1024() in the loops below.
    const int16_t *lut = sinLut();

    for (int yy = 0; yy < rows; yy++) {
        const int y = y0 + yy;
        const RowSetup rs = computeRowSetup(y, lut);
        uint32_t phaseQ[K] = {rs.phase[0], rs.phase[1], rs.phase[2]};
        uint16_t *row = dst + static_cast<size_t>(yy) * w;
        const uint16_t *rowBase = rs.rowBase;

        int x = 0;
        int32_t sumCur = rs.sumCur0;

        // Main loop: GRID is a compile-time constant here (the tail below
        // handles any remainder), so the step multiply becomes a shift and
        // the 4-wide inner body is fully unrolled — no per-span branch, no
        // loop-trip-count check inside the hot path.
        while (x + GRID <= w) {
            const uint32_t np0 = phaseQ[0] + (g_stepQ[0] << 2);
            const uint32_t np1 = phaseQ[1] + (g_stepQ[1] << 2);
            const uint32_t np2 = phaseQ[2] + (g_stepQ[2] << 2);
            const int32_t sumNext = lut[(np0 >> PHASE_SHIFT) & (SIN_N - 1)] + lut[(np1 >> PHASE_SHIFT) & (SIN_N - 1)] +
                                     lut[(np2 >> PHASE_SHIFT) & (SIN_N - 1)];
            const int32_t stepInterp = (sumNext - sumCur) >> 2; // GRID==4

            int32_t val = sumCur;
            int32_t m = val >> 31;
            row[x + 0] = rowBase[shapeLUT[(val ^ m) - m] * 4 + 0];
            val += stepInterp;
            m = val >> 31;
            row[x + 1] = rowBase[shapeLUT[(val ^ m) - m] * 4 + 1];
            val += stepInterp;
            m = val >> 31;
            row[x + 2] = rowBase[shapeLUT[(val ^ m) - m] * 4 + 2];
            val += stepInterp;
            m = val >> 31;
            row[x + 3] = rowBase[shapeLUT[(val ^ m) - m] * 4 + 3];

            phaseQ[0] = np0;
            phaseQ[1] = np1;
            phaseQ[2] = np2;
            sumCur = sumNext;
            x += GRID;
        }

        // Tail shorter than GRID (only when w % GRID != 0): exact per-pixel
        // evaluation, no interpolation. 480 % 4 == 0 on the real panel (and
        // 240 % 4 == 0 at half resolution), so this path is untaken there.
        for (int i = 0; x < w; i++, x++) {
            const int32_t sum = lut[((phaseQ[0] + g_stepQ[0] * static_cast<uint32_t>(i)) >> PHASE_SHIFT) & (SIN_N - 1)] +
                                 lut[((phaseQ[1] + g_stepQ[1] * static_cast<uint32_t>(i)) >> PHASE_SHIFT) & (SIN_N - 1)] +
                                 lut[((phaseQ[2] + g_stepQ[2] * static_cast<uint32_t>(i)) >> PHASE_SHIFT) & (SIN_N - 1)];
            const int32_t m = sum >> 31;
            row[x] = rowBase[shapeLUT[(sum ^ m) - m] * 4 + (x & 3)];
        }
    }
}

#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
// One row's GRID-aligned span loop (see the file header for the algorithm:
// three DDS sine waves summed, coarse-grid interpolated every GRID=4
// columns, then the branchless-abs + two-stage gather, shapeLUT, then the
// per-column-phase RGB565 palette, that turns the wave-sum magnitude into
// a pixel). w is guaranteed a multiple of GRID by the caller (band(), which
// falls back to bandRef for the case where it is not, never hit on the
// real 480- or 240-wide panel, both multiples of 4); this kernel does not
// handle a tail.
//
// Scalar, not PIE: the two per-pixel lookups are data-dependent gathers and
// PIE has no gather instruction (ASM_BRIEF.md, "Where PIE does not fit").
// What IS hand-optimized is register allocation and instruction scheduling.
// xtensa-asm14/AnimCaustics.S (the real device compiler, before this pass)
// showed the compiled band()'s main span loop (.L9) spilling six
// loop-invariant values to the stack and reloading them every 4-pixel span -
// three rgb565 bucket pointers (rgb1/rgb2/rgb3 of the four original
// [dither][shading] buckets; rgb0 stayed resident) and the g_stepQ[0]<<2 /
// g_stepQ[1]<<2 per-span phase increments, six `l32i ..., sp, ...` per
// iteration for values that never change across the whole row. This kernel
// pins every one of them in a register for the row's entire span loop
// instead, at the cost of restructuring rgbLUT's layout so four pointers
// collapse to one (see the file header's "asm-caustics" section).
//
// Register budget: windowed ABI leaves a2-a15 usable (a0 is the return
// address `retw.n` needs intact, a1 the stack pointer), 14 registers, and
// this kernel uses every one of them, pinned for the whole asm block (GCC
// assigns the actual physical register per operand; only the roles are
// named here):
//   lut, shapeLUT, rgbBase, step0/1/2         read-only inputs        (6)
//   row, phase0/1/2, val                      mutated in place        (5)
//   cnt                                       nSpans for `loop`'s setup
//                                              instruction only, then dead
//                                              and immediately reused as
//                                              the running sumNext         (1)
//   stepI, t                                  pure scratch                (2)
//                                                                    ------
//                                                                       14
// There is no slack left for a deeper software-pipelined schedule (e.g.
// holding two pixels' in-flight loads at once), see below for where that
// bites.
//
// `loop` (hardware zero-overhead loop), not an explicit decrement+branch:
// the two are equivalent here except `loop` frees the nSpans register for
// reuse the instant it has latched LCOUNT, which is what makes the
// 14-operand budget fit at all (an explicit counter would need to persist
// across the whole body, costing a 15th register this file does not have).
// Safe against nesting with band()'s own per-row loop: a loop body
// containing a CALL never gets GCC's zero-overhead LOOP instruction
// (OPTIMIZE.md's addendum), confirmed in xtensa-asm14/AnimCaustics.S,
// band()'s row loop (which now calls this kernel) compiles to a plain
// compare-and-branch, so there is only ever one level of
// LBEG/LEND/LCOUNT live at a time; this function does not need to save or
// restore them.
//
// Per-span shape: 16 instructions for the gather + interpolation setup, 7
// per pixel x 4 = 28, +1 pointer increment = 45. The phase-advance add for
// wave k+1 is placed right after wave k's sin1024 load specifically as
// filler, it is needed anyway, and placing it there hides the load-use
// stall (one cycle when the very next instruction consumes a load result,
// per ASM_BRIEF.md's Xtensa scalar facts) for free instead of paying it.
// The same trick runs inside each pixel: "val += stepInterp" for the NEXT
// pixel is issued between the shapeLUT load and the addx8 that consumes its
// result. Two places have no spare register left for a filler: the third
// wave's sin1024 load (immediately summed into cnt, stepI's and cnt's own
// earlier loads had a filler, val2's does not) and every pixel's palette
// load into its store (t is the only scratch register and it holds the
// exact value being stored, so there is nothing independent to interleave
// there). That is 1 + 4 = 5 stalls per 4-pixel span, 1.25/pixel, on top of
// the 45/4 = 11.25 static instructions/pixel, see the report for the
// resulting cycles/pixel estimate.
__attribute__((noinline)) static void causticsRowKernel(uint16_t *row, const int16_t *lut, const uint8_t *shapeLUT,
                                                         const uint16_t *rgbRowBase, uint32_t phase0, uint32_t phase1,
                                                         uint32_t phase2, uint32_t step0x4, uint32_t step1x4,
                                                         uint32_t step2x4, int32_t sumCur0, int nSpans) {
    int32_t val = sumCur0;
    int32_t cnt = nSpans; // loop trip count first, then reused as sumNext
    int32_t stepI, t;     // pure scratch, no meaningful value on entry
    asm volatile("loop %[cnt], 2f\n"
                 // --- span-level: advance phase, gather the exact sum at the next span ---
                 "add %[phase0], %[phase0], %[step0]\n"
                 "extui %[t], %[phase0], 22, 10\n"
                 "addx2 %[t], %[t], %[lut]\n"
                 "l16si %[cnt], %[t], 0\n"              // cnt = val0
                 "add %[phase1], %[phase1], %[step1]\n" // filler: val0's load-use gap
                 "extui %[t], %[phase1], 22, 10\n"
                 "addx2 %[t], %[t], %[lut]\n"
                 "l16si %[stepI], %[t], 0\n"            // stepI = val1
                 "add %[phase2], %[phase2], %[step2]\n" // filler: val1's load-use gap
                 "extui %[t], %[phase2], 22, 10\n"
                 "addx2 %[t], %[t], %[lut]\n"
                 "l16si %[t], %[t], 0\n"                  // t = val2 (no filler slot; see header)
                 "add %[cnt], %[cnt], %[stepI]\n"         // cnt = val0+val1 (both loaded long enough ago)
                 "add %[cnt], %[cnt], %[t]\n"             // cnt = sumNext (pays val2's stall)
                 "sub %[stepI], %[cnt], %[val]\n"         // stepI = sumNext - sumCur (ALU->ALU, no stall)
                 "srai %[stepI], %[stepI], 2\n"           // stepI = stepInterp
                 // --- pixel 0 (val == sumCur, unmodified so far) ---
                 "abs %[t], %[val]\n"
                 "add %[t], %[t], %[shapeLUT]\n"
                 "l8ui %[t], %[t], 0\n"           // t = shading0
                 "add %[val], %[val], %[stepI]\n" // val = val_p1, filler for shading0's load-use gap
                 "addx8 %[t], %[t], %[rgbBase]\n"
                 "l16ui %[t], %[t], 0\n" // t = pixel0 color
                 "s16i %[t], %[row], 0\n" // no spare register for a filler here (see header)
                 // --- pixel 1 (val == val_p1) ---
                 "abs %[t], %[val]\n"
                 "add %[t], %[t], %[shapeLUT]\n"
                 "l8ui %[t], %[t], 0\n"
                 "add %[val], %[val], %[stepI]\n" // val = val_p2
                 "addx8 %[t], %[t], %[rgbBase]\n"
                 "l16ui %[t], %[t], 2\n"
                 "s16i %[t], %[row], 2\n"
                 // --- pixel 2 (val == val_p2) ---
                 "abs %[t], %[val]\n"
                 "add %[t], %[t], %[shapeLUT]\n"
                 "l8ui %[t], %[t], 0\n"
                 "add %[val], %[val], %[stepI]\n" // val = val_p3
                 "addx8 %[t], %[t], %[rgbBase]\n"
                 "l16ui %[t], %[t], 4\n"
                 "s16i %[t], %[row], 4\n"
                 // --- pixel 3 (val == val_p3) ---
                 "abs %[t], %[val]\n"
                 "add %[t], %[t], %[shapeLUT]\n"
                 "l8ui %[t], %[t], 0\n"
                 "or %[val], %[cnt], %[cnt]\n" // val = sumNext for the next span, filler
                 "addx8 %[t], %[t], %[rgbBase]\n"
                 "l16ui %[t], %[t], 6\n"
                 "s16i %[t], %[row], 6\n"
                 "addi %[row], %[row], 8\n"
                 "2:\n"
                 : [row] "+r"(row), [phase0] "+r"(phase0), [phase1] "+r"(phase1), [phase2] "+r"(phase2),
                   [val] "+r"(val), [cnt] "+r"(cnt), [stepI] "=&r"(stepI), [t] "=&r"(t)
                 : [lut] "r"(lut), [shapeLUT] "r"(shapeLUT), [rgbBase] "r"(rgbRowBase), [step0] "r"(step0x4),
                   [step1] "r"(step1x4), [step2] "r"(step2x4)
                 : "memory");
}
#endif

void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
    if (w % GRID != 0) {
        // Defensive only: the real panel is 480 (full res) or 240 (half
        // res), both multiples of GRID=4, so this is never taken on device.
        // The kernel handles GRID-aligned spans exclusively (see its
        // comment); anything else falls back to the portable, always-
        // correct reference.
        bandRef(dst, y0, rows, w, tMs, p);
        return;
    }
    const int16_t *lut = sinLut();
    const uint32_t step0x4 = g_stepQ[0] << 2;
    const uint32_t step1x4 = g_stepQ[1] << 2;
    const uint32_t step2x4 = g_stepQ[2] << 2;
    for (int yy = 0; yy < rows; yy++) {
        const int y = y0 + yy;
        const RowSetup rs = computeRowSetup(y, lut);
        causticsRowKernel(dst + static_cast<size_t>(yy) * w, lut, shapeLUT, rs.rowBase, rs.phase[0], rs.phase[1],
                           rs.phase[2], step0x4, step1x4, step2x4, rs.sumCur0, w / GRID);
    }
#else
    bandRef(dst, y0, rows, w, tMs, p);
#endif
}

void release() {
    releaseTable(rgbLUT, static_cast<size_t>(DITHER_N) * SHADE_LEVELS * sizeof(uint16_t));
    releaseTable(shapeLUT, static_cast<size_t>(SHAPE_N));
    lastThemeGen = 0xFFFFFFFF;
}

} // namespace

extern const BgAnimation bg_anim_caustics;
const BgAnimation bg_anim_caustics = {
    "caustics",
    "Caustics",
    {{"speed", "Drift speed", 50}, {"scale", "Cell scale", 45}, {"contrast", "Contrast", 55}, {nullptr, nullptr, 0}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
