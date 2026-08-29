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

// Sanity-check variant: packs two adjacent 32-bit `v | (v<<16)` words (four
// source pixels) into one 64-bit store via memcpy (portable, no
// alignment/aliasing UB), to validate the pairing arithmetic before trying a
// genuinely wider hardware store. Same values, same order as expandRow_ref;
// only the store grouping differs. See halfres_expand.cpp for why this is
// not expected to help on the real device (Xtensa has no native 64-bit
// integer store).
void expandRow_wide64(const uint16_t *__restrict src, uint16_t *__restrict row0, uint16_t *__restrict row1, int rw);

// Pure-C model of expandRow_pie_asm's approach below: four source pixels
// computed independently (same `v | (v<<16)` as expandRow_ref), then
// flushed as a group of four -- the same grouping the real asm flushes with
// one EE.VST.128.IP. Bit-identical to expandRow_ref for any input; this is
// the bit-exactness gate the real asm variant is checked against in spirit
// (registered separately per the span_scan pie_model convention), not
// merely another expandRow_ref reimplementation.
void expandRow_pie_model(const uint16_t *__restrict src, uint16_t *__restrict row0, uint16_t *__restrict row1,
                          int rw);

#if defined(__XTENSA__)
// Real ESP32-S3 PIE implementation of the "wider store" hypothesis this
// kernel exists to test (see BASELINE-OVERLAY.md and the comment above
// expandRow_pie_asm in halfres_expand.cpp for the full derivation and the
// confirmed-real instructions it relies on). Xtensa-only inline asm: cannot
// build, and has never been run, on this x86 host -- see piePending on this
// variant's kExpandRowVariants entry. Falls back to plain scalar (identical
// to expandRow_ref) for any row whose pointer is not 16-byte aligned, so
// this is never less correct than expandRow_ref, only faster on the
// aligned common case (real band[] rows always are -- see the comment in
// halfres_expand.cpp).
void expandRow_pie_asm(const uint16_t *__restrict src, uint16_t *__restrict row0, uint16_t *__restrict row1, int rw);
#endif

struct ExpandRowVariant {
    const char *name;
    ExpandRowFn fn;
    bool bitExactRequired;
    bool piePending;
};

extern const ExpandRowVariant kExpandRowVariants[];
extern const int kExpandRowVariantCount;

} // namespace ovb
