#include "span_scan.h"

namespace ovb {

// Byte-identical to the body of the `for (int y = rowY0; y < rowY1; y++)`
// loop in SleepAnimation::publishOverlayRanges (the `sy >= 0 && sy < h`
// branch), with the surrounding range/offset bookkeeping stripped out: this
// function is exactly the per-row work, called once per row by the caller.
int scanRow_ref(const uint8_t *__restrict rowAlpha3, int panelW, uint32_t *__restrict rowRuns,
                uint8_t *__restrict cellRow) {
    const uint8_t *a = rowAlpha3;
    int nRuns = 0;
    int runStart = -1;
    for (int x = 0; x < panelW; x++, a += 3) {
        if (*a != 0) {
            if (runStart < 0) {
                runStart = x;
            }
            if (cellRow != nullptr) {
                uint8_t &c = cellRow[x >> SCRIM_SHIFT];
                if (*a > c) {
                    c = *a;
                }
            }
            continue;
        }
        if (runStart < 0) {
            continue;
        }
        nRuns = emitRun_ref(rowRuns, nRuns, runStart, x, RUN_GAP_MERGE);
        runStart = -1;
    }
    if (runStart >= 0) {
        nRuns = emitRun_ref(rowRuns, nRuns, runStart, panelW, RUN_GAP_MERGE);
    }
    return nRuns;
}

// Variant table. Workers append their own entry here (and their function
// lives in this same file) -- the runner and golden-comparison harness pick
// up every row automatically.
const ScanRowVariant kScanRowVariants[] = {
    {"ref_scalar", &scanRow_ref, true, false},
};
const int kScanRowVariantCount = sizeof(kScanRowVariants) / sizeof(kScanRowVariants[0]);

} // namespace ovb
