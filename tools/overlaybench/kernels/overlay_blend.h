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

// scrimRow_ref with the per-cell `inv == SCRIM_INV_NONE` skip replaced by an
// unconditional call to scrimCell_ref. 32 is scale565's identity factor (see
// scale565_ref in common.h and the derivation comment on scale565 in
// SleepAnimation.cpp), so this is a true no-op substitution for the skip,
// not an approximation -- the same branchless trick the shipped scrimRowPie
// already uses for exactly this case ("a lane cannot branch... that factor
// is an exact identity"). BASELINE-OVERLAY.md's hypothesis was that this
// alone would recover GCC's zero-overhead LOOP; it does not (checked via
// ./asm.sh overlay_blend -- see the comment on the definition in
// overlay_blend.cpp for why: register pressure from scrimCell_ref's two
// parallel words, not the branch, is the real blocker). scrimRow_branchlessWord
// below is the variant that actually recovers the LOOP.
void scrimRow_branchless(uint16_t *__restrict dst, const uint8_t *__restrict invRow,
                         const uint32_t *__restrict haloRuns, int nHalo, int w);

// scrimRow_branchless restructured to one word (two pixels) per trip
// instead of one cell (four pixels) -- halving the loop body's live
// temporaries is what actually recovers a hardware LOOP (confirmed via
// ./asm.sh overlay_blend: 1 zero-overhead loop, 64 instructions, against
// zero loops for both scrimRow_ref and scrimRow_branchless). See the
// comment on the definition in overlay_blend.cpp for the full comparison.
void scrimRow_branchlessWord(uint16_t *__restrict dst, const uint8_t *__restrict invRow,
                             const uint32_t *__restrict haloRuns, int nHalo, int w);

// Pass 2, vectorised: alpha-composite eight pixels at a time instead of one.
// Pure-C model of the design a real PIE implementation would use -- gathers
// each 8-pixel group's colour+alpha out of the stride-3 source into a
// contiguous staging buffer (there is no gather instruction in this ISA, the
// same reason the shipped scrimRowPie pre-expands its per-cell factors with
// a scalar pass before scale565Oct runs), then takes one of three paths per
// group: a whole-group opaque copy, a whole-group vector blend, or (for a
// group mixing opaque and non-opaque pixels) a scalar fallback identical to
// blendRow_ref's own per-pixel body. See the block comment above the
// definition in overlay_blend.cpp for the full derivation, including the
// worked counterexample proving a==255 needs the copy path exactly (the
// general blend formula is provably NOT equal to a plain copy for that
// case -- it is off by one in some channel whenever a background channel is
// less than the foreground one), so the group-level 3-way split is required
// for bit-exactness, not just an optimization.
void blendRow_pie_model(uint16_t *__restrict dst, const uint8_t *__restrict colour, const uint32_t *__restrict runs,
                        int nRuns);

#if defined(__XTENSA__)
// Real ESP32-S3 PIE attempt at blendRow_pie_model's design, guarded because
// EE.* is Xtensa-only inline asm and cannot build for the host (see
// scanRow_pie_asm in span_scan.h for the same pattern). piePending: only
// ever checked by feeding candidate mnemonics to the real assembler
// (xtensa-esp32s3-elf-as, via a standalone probe file, then ./asm.sh
// overlay_blend for this function itself) -- never run, on device or QEMU.
// Only the "every lane in the group is opaque" path is done in real vector
// instructions (a plain aligned copy, no arithmetic); the general and mixed
// paths fall back to the proven scalar body, so this is always at least as
// correct as blendRow_pie_model, never less. See the comment above the
// definition in overlay_blend.cpp for exactly which EE.* mnemonics were
// confirmed to exist this way (ee.vadds.s16 in particular -- the add
// blendRow_pie_model's general-path arithmetic needs and scale565Oct never
// did) and why the general path's real asm was not attempted blind despite
// that.
void blendRow_pie_asm(uint16_t *__restrict dst, const uint8_t *__restrict colour, const uint32_t *__restrict runs,
                      int nRuns);
#endif

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

// Pass 1 replaced by scrimRow_branchless, pass 2 unchanged. Isolates
// direction 1 (the recovered hardware loop) from direction 2 below.
void blendStage_scrimBranchless(uint16_t *__restrict dst, const uint8_t *__restrict invRow,
                                const uint32_t *__restrict haloRuns, int nHalo, const uint8_t *__restrict colour,
                                const uint32_t *__restrict runs, int nRuns, int w);

// Pass 1 unchanged, pass 2 replaced by blendRow_pie_model. Isolates
// direction 2 (the vector composite model) from direction 1 above.
void blendStage_pieModel(uint16_t *__restrict dst, const uint8_t *__restrict invRow,
                         const uint32_t *__restrict haloRuns, int nHalo, const uint8_t *__restrict colour,
                         const uint32_t *__restrict runs, int nRuns, int w);

// Pass 1 replaced by scrimRow_branchlessWord (the variant that actually
// recovers a hardware loop), pass 2 unchanged.
void blendStage_scrimBranchlessWord(uint16_t *__restrict dst, const uint8_t *__restrict invRow,
                                    const uint32_t *__restrict haloRuns, int nHalo, const uint8_t *__restrict colour,
                                    const uint32_t *__restrict runs, int nRuns, int w);

// Both directions together: the best-of variant.
void blendStage_pieModelCombined(uint16_t *__restrict dst, const uint8_t *__restrict invRow,
                                 const uint32_t *__restrict haloRuns, int nHalo, const uint8_t *__restrict colour,
                                 const uint32_t *__restrict runs, int nRuns, int w);

#if defined(__XTENSA__)
// scrimRow_branchlessWord + the real (partial) blendRow_pie_asm. Xtensa-only,
// like blendRow_pie_asm itself; not in the host-buildable variant table.
void blendStage_pieAsm(uint16_t *__restrict dst, const uint8_t *__restrict invRow,
                       const uint32_t *__restrict haloRuns, int nHalo, const uint8_t *__restrict colour,
                       const uint32_t *__restrict runs, int nRuns, int w);
#endif

struct BlendStageVariant {
    const char *name;
    BlendStageFn fn;
    bool bitExactRequired;
    bool piePending;
};

extern const BlendStageVariant kBlendStageVariants[];
extern const int kBlendStageVariantCount;

} // namespace ovb
