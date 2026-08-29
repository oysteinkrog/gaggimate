// Kernel 2: scrim build.
//
// Extracted from SleepAnimation::buildScrim and its scrimTap3 helper
// (SleepAnimation.cpp ~109-129, ~1429-1482), UI-task side. Runs six
// separable 3-tap passes (two max/dilate + one 1-2-1 smooth, each applied
// horizontally then vertically) over the SCRIM_W x SCRIM_H (120x120) cell
// grid of peak alpha, then quantizes the result to a 1/32 dim factor and
// emits per-cell-row halo runs.
//
// Contract: same six-pass order, same edge-clamp behaviour in scrimTap3,
// same quantization rounding, same halo run encoding. Bit-exact against the
// golden is mandatory.
#pragma once
#include "../common.h"

namespace ovb {

// One separable 3-tap pass. Byte-identical to scrimTap3 in SleepAnimation.cpp.
using ScrimTap3Fn = void (*)(const uint8_t *__restrict src, uint8_t *__restrict dst, int lines, int n, int lineStep,
                             int step, bool useMax);
void scrimTap3_ref(const uint8_t *__restrict src, uint8_t *__restrict dst, int lines, int n, int lineStep, int step,
                   bool useMax);

// Full six-pass build + halo run emission. `src` is the SCRIM_W*SCRIM_H peak-
// alpha grid (ov.scrimSrc); `tmp` and `out` are same-size scratch/halo grids
// (ov.scrim after the call holds what the real field does). `haloRuns` is
// SCRIM_H*RUNS_PER_ROW uint32_t, `haloN` is SCRIM_H bytes -- both filled the
// same way buildScrim fills ov.haloRuns/ov.haloN. `q8` is the scrim strength
// (0..256, Q8), matching scrimQ8.
using BuildScrimFn = void (*)(const uint8_t *__restrict src, uint8_t *__restrict tmp, uint8_t *__restrict out, int sw,
                              int sh, int q8, uint32_t *__restrict haloRuns, uint8_t *__restrict haloN);
void buildScrim_ref(const uint8_t *__restrict src, uint8_t *__restrict tmp, uint8_t *__restrict out, int sw, int sh,
                    int q8, uint32_t *__restrict haloRuns, uint8_t *__restrict haloN);

// buildScrim_ref with the two "vertical" dilate passes (3 and 4 -- the only
// ADJACENT pair of same-shape passes in the fixed H,H,V,V,H,V order)
// rewritten as: transpose once, run both as sequential "horizontal-shaped"
// calls to scrimTap3_ref on the transposed grid (same 3-tap window, same
// clamp bound, just walking transposed coordinates -- a mathematical no-op,
// not a reordering of the iterative dependency), transpose back once. Pass 6
// (the third vertical pass, isolated between two horizontal passes) is left
// exactly as buildScrim_ref computes it -- see the worker's report for why
// bracketing an isolated single pass with its own transpose-in/transpose-out
// is not expected to pay for itself. Bit-exact by construction; see
// scrim_build.cpp for the parameter-mapping proof.
void buildScrim_transposed34(const uint8_t *__restrict src, uint8_t *__restrict tmp, uint8_t *__restrict out, int sw,
                             int sh, int q8, uint32_t *__restrict haloRuns, uint8_t *__restrict haloN);

// buildScrim_transposed34 taken further: transposes around pass 6 as well
// (a second, separate transpose bracket), so all six passes run sequential
// rather than strided. Costs 4 transposes total instead of 2. Registered
// mainly as a comparison point for whether the extra two transposes are
// worth eliminating the one remaining strided pass -- see the report.
void buildScrim_transposed_all(const uint8_t *__restrict src, uint8_t *__restrict tmp, uint8_t *__restrict out, int sw,
                               int sh, int q8, uint32_t *__restrict haloRuns, uint8_t *__restrict haloN);

struct BuildScrimVariant {
    const char *name;
    BuildScrimFn fn;
    bool bitExactRequired;
    bool piePending;
};

extern const BuildScrimVariant kBuildScrimVariants[];
extern const int kBuildScrimVariantCount;

} // namespace ovb
