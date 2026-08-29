// Kernel 1: overlay span scan.
//
// Extracted from SleepAnimation::publishOverlayRanges's per-row inner loop
// (SleepAnimation.cpp ~1378-1414), UI-task side. Scans the coverage (A8) byte
// of an interleaved RGB565+A8 overlay row (LV_IMG_CF_TRUE_COLOR_ALPHA,
// LV_COLOR_16_SWAP=0: 2 colour bytes little-endian + 1 alpha byte, 3 B/px),
// emits merged runs of non-zero coverage, and -- when a scrim cell row is
// live -- accumulates each cell's peak alpha (not average; see the comment
// on cell[] in the real function).
//
// Contract for every variant: same inputs, same run encoding
// (packed [x0 | x1<<16), half-open, ascending, RUNS_PER_ROW-bounded via
// emitRun_ref), same cell peak-alpha accumulation. Bit-exact against the
// golden is mandatory; only speed may differ.
#pragma once
#include "../common.h"

namespace ovb {

// Scans one panel row. `rowAlpha3` points at the coverage byte of pixel 0
// (i.e. the real code's `ov.buf + (sy*w+xoff)*3 + 2`); consecutive pixels are
// 3 bytes apart. `cellRow`, if non-null, is the scrim cell row this panel row
// belongs to (SCRIM_SHIFT panel rows share one cell row) and is updated with
// a running max, exactly like the firmware's per-publish accumulation -- the
// caller must memset the cell row to 0 before the first of the SCRIM_SHIFT
// rows that feed it, same as publishOverlayRanges does per range.
// Returns nRuns (also written to *nRunsOut for the variant-table signature).
using ScanRowFn = int (*)(const uint8_t *__restrict rowAlpha3, int panelW, uint32_t *__restrict rowRuns,
                          uint8_t *__restrict cellRow);

int scanRow_ref(const uint8_t *__restrict rowAlpha3, int panelW, uint32_t *__restrict rowRuns,
                uint8_t *__restrict cellRow);

// scanRow_ref with the `cellRow != nullptr` test hoisted out of the per-pixel
// loop. In the real caller (publishOverlayRanges) doScrim is a per-publish
// constant -- every row of one publish passes either a live cellRow or null,
// never a mix -- so the null check scanRow_ref repeats for every covered
// pixel is loop-invariant. This dispatches once per call (a single branch)
// to a template instantiated for both cases, so the compiled loop body for
// each case never mentions the other. No other behavioural change.
int scanRow_spec(const uint8_t *__restrict rowAlpha3, int panelW, uint32_t *__restrict rowRuns,
                  uint8_t *__restrict cellRow);

// scanRow_spec plus a whole-block emptiness fast path. Real content is
// sparse (see the file header comment and BASELINE-OVERLAY.md); most
// BLOCK-pixel spans have zero coverage. Each block is probed with a
// branch-free OR-accumulate over its alpha bytes (a fixed-trip-count loop,
// a hardware-loop candidate where the branchy per-pixel body is not); a
// zero result skips the block outright (closing any run left open from a
// prior block, exactly where the per-pixel scan would have closed it -- see
// the correctness note above scanRowBlockT's definition in span_scan.cpp).
// A nonzero result falls back to exactly scanRow_spec's per-pixel body for
// that block only. Every emitRun_ref call this produces, for any input, is
// identical in order and arguments to what scanRow_ref would have made --
// see the block-skip correctness note in span_scan.cpp.
int scanRow_block16(const uint8_t *__restrict rowAlpha3, int panelW, uint32_t *__restrict rowRuns,
                     uint8_t *__restrict cellRow);
int scanRow_block32(const uint8_t *__restrict rowAlpha3, int panelW, uint32_t *__restrict rowRuns,
                     uint8_t *__restrict cellRow);

// Pure-C model of a PIE (ESP32-S3 vector unit) block-emptiness probe: 16
// pixels (48 bytes, exactly three 128-bit lanes) at a time, masked so only
// the alpha lane of each pixel survives, OR-reduced across the three lanes,
// then reduced to one word -- see the block comment above scanRowPieModelT
// in span_scan.cpp for the lane layout. Bit-identical output to
// scanRow_block16 (both are "is this block ever nonzero", just computed two
// different ways); registered separately because THIS is the function whose
// arithmetic scanRow_pie_asm's inline asm must match, so it is the
// bit-exactness gate for that PIE variant, not merely another block-skip
// implementation.
int scanRow_pie_model(const uint8_t *__restrict rowAlpha3, int panelW, uint32_t *__restrict rowRuns,
                       uint8_t *__restrict cellRow);

#if defined(__XTENSA__)
// Real ESP32-S3 PIE (EE.* vector) implementation of the same probe
// scanRow_pie_model describes, guarded because EE.* is Xtensa-only inline
// asm and cannot build for the host. Falls back to the plain block-skip
// scalar path (scanRow_block16's core) whenever the fast lane requires
// something it does not handle -- see the guard conditions in
// span_scan.cpp -- so it is always at least as correct as scanRow_block16,
// never less. piePending in its kScanRowVariants entry: this harness cannot
// execute Xtensa asm, so it has only ever been checked by inspection; it
// needs a real-device or QEMU run before it ships.
int scanRow_pie_asm(const uint8_t *__restrict rowAlpha3, int panelW, uint32_t *__restrict rowRuns,
                     uint8_t *__restrict cellRow);
#endif

struct ScanRowVariant {
    const char *name;
    ScanRowFn fn;
    bool bitExactRequired; // true unless it is a PIE/device-only entry
    bool piePending;       // true if correctness is only checked via a C model and needs device/QEMU validation
};

extern const ScanRowVariant kScanRowVariants[];
extern const int kScanRowVariantCount;

} // namespace ovb
