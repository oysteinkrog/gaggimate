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

struct ScanRowVariant {
    const char *name;
    ScanRowFn fn;
    bool bitExactRequired; // true unless it is a PIE/device-only entry
    bool piePending;       // true if correctness is only checked via a C model and needs device/QEMU validation
};

extern const ScanRowVariant kScanRowVariants[];
extern const int kScanRowVariantCount;

} // namespace ovb
