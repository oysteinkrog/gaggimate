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

struct BuildScrimVariant {
    const char *name;
    BuildScrimFn fn;
    bool bitExactRequired;
    bool piePending;
};

extern const BuildScrimVariant kBuildScrimVariants[];
extern const int kBuildScrimVariantCount;

} // namespace ovb
