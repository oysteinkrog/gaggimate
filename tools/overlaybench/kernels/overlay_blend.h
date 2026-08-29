// Kernel 3: overlay blend.
//
// Extracted from the per-row body of SleepAnimation::renderFrame's band loop
// (SleepAnimation.cpp ~2494-2549), the section timed as profBlendUs /
// accBlendUs. Two passes per row, both included here because they are
// timed together on device and share the band-buffer row:
//   pass 1 (scrimRow/scrimCell) -- dim the halo toward black, cell-run driven
//   pass 2 (blendRow)           -- alpha-composite the overlay glyphs, using
//                                  the per-row glyph runs, over the (now
//                                  dimmed) band row
//
// scrimRowPie/scale565Oct already exist in the firmware as a hand-written
// ESP32-S3 PIE (vector, "EE.*") variant of pass 1 -- see the block comment
// above scale565Oct in SleepAnimation.cpp for the derivation and the
// exhaustive-equivalence proof against scale565_ref. That proof is why
// scrimRow_ref doubles as the bit-exact host model for the shipped PIE path;
// there is nothing left to "port" for correctness, only to reproduce for a
// worker who wants the vector approach as a starting point for blendRow.
//
// Contract: same two-pass order, same scrim/halo skip-when-SCRIM_INV_NONE
// behaviour, same blend565 math, same opaque (a==255) fast path. Bit-exact
// against the golden is mandatory.
#pragma once
#include "../common.h"

namespace ovb {

// Pass 2 alone: composite `nRuns` glyph runs from `colour` (RGB565LE+A8,
// 3 B/px, exactly like the firmware's overlay snapshot) onto `dst` (one
// band row, RGB565). Byte-identical to blendRow in SleepAnimation.cpp.
using BlendRowFn = void (*)(uint16_t *__restrict dst, const uint8_t *__restrict colour,
                            const uint32_t *__restrict runs, int nRuns);
void blendRow_ref(uint16_t *__restrict dst, const uint8_t *__restrict colour, const uint32_t *__restrict runs,
                  int nRuns);

// Pass 1 alone: dim `dst` toward black over `nHalo` cell-runs from `invRow`
// (SCRIM_W bytes, one 1/32 dim factor per cell). Byte-identical to scrimRow
// in SleepAnimation.cpp (the scalar path; pieScrim off).
using ScrimRowFn = void (*)(uint16_t *__restrict dst, const uint8_t *__restrict invRow,
                            const uint32_t *__restrict haloRuns, int nHalo, int w);
void scrimRow_ref(uint16_t *__restrict dst, const uint8_t *__restrict invRow, const uint32_t *__restrict haloRuns,
                  int nHalo, int w);

// Both passes, in the firmware's order, on one row -- this is what
// profBlendUs actually times per row. This is the primary optimization
// target: a variant may fuse the two passes (e.g. skip touching a pixel
// scrim just dimmed and blend is about to fully overwrite) as long as the
// output stays bit-exact.
using BlendStageFn = void (*)(uint16_t *__restrict dst, const uint8_t *__restrict invRow,
                              const uint32_t *__restrict haloRuns, int nHalo, const uint8_t *__restrict colour,
                              const uint32_t *__restrict runs, int nRuns, int w);
void blendStage_ref(uint16_t *__restrict dst, const uint8_t *__restrict invRow, const uint32_t *__restrict haloRuns,
                    int nHalo, const uint8_t *__restrict colour, const uint32_t *__restrict runs, int nRuns, int w);

struct BlendStageVariant {
    const char *name;
    BlendStageFn fn;
    bool bitExactRequired;
    bool piePending;
};

extern const BlendStageVariant kBlendStageVariants[];
extern const int kBlendStageVariantCount;

} // namespace ovb
