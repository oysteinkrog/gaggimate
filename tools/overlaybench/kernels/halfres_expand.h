// Kernel 4: half-res expand.
//
// Extracted from the half-resolution 2x-in-both-axes expansion loop in
// SleepAnimation::renderFrame (SleepAnimation.cpp ~2277-2318, profExpandUs).
// One half-width source row (rw = w/2 pixels, RGB565) is horizontally
// doubled into row0 (two 16-bit source pixels packed into one 32-bit store),
// then the same computation is repeated from `src` into row1 -- NOT a memcpy
// of row0 -- because row0/row1 are `w` pixels apart in the destination
// (PSRAM band buffer) and re-deriving from the cache-resident `src` beat
// touching two far-apart PSRAM cache lines per iteration (measured on
// device: see the comment block in renderFrame for the 31.2 ms/frame it cost
// the other way).
//
// Contract: same pixel-doubling arithmetic (`v | (v << 16)`), same
// row0-then-row1 order and independent re-read of `src` for row1 (a variant
// MAY copy row0 into row1 instead, but only if it is faster on the *device*
// cost model in OPTIMIZE.md, not just on host -- say so explicitly and keep
// the ref semantics as the fallback). Bit-exact against the golden is
// mandatory regardless of the internal approach.
#pragma once
#include "../common.h"

namespace ovb {

// Expands one half-width, half-height source row into two full-width output
// rows. `src` is rw pixels; `row0`/`row1` are 2*rw (== w) pixels each.
using ExpandRowFn = void (*)(const uint16_t *__restrict src, uint16_t *__restrict row0, uint16_t *__restrict row1,
                             int rw);
void expandRow_ref(const uint16_t *__restrict src, uint16_t *__restrict row0, uint16_t *__restrict row1, int rw);

struct ExpandRowVariant {
    const char *name;
    ExpandRowFn fn;
    bool bitExactRequired;
    bool piePending;
};

extern const ExpandRowVariant kExpandRowVariants[];
extern const int kExpandRowVariantCount;

} // namespace ovb
