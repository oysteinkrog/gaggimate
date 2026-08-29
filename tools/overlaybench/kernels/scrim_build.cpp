#include "scrim_build.h"

namespace ovb {

// Byte-identical to scrimTap3 in SleepAnimation.cpp.
void scrimTap3_ref(const uint8_t *__restrict src, uint8_t *__restrict dst, int lines, int n, int lineStep, int step,
                   bool useMax) {
    for (int l = 0; l < lines; l++) {
        const uint8_t *sp = src + static_cast<size_t>(l) * lineStep;
        uint8_t *dp = dst + static_cast<size_t>(l) * lineStep;
        for (int i = 0; i < n; i++) {
            const int a = sp[static_cast<size_t>(i > 0 ? i - 1 : 0) * step];
            const int b = sp[static_cast<size_t>(i) * step];
            const int c = sp[static_cast<size_t>(i < n - 1 ? i + 1 : n - 1) * step];
            int v;
            if (useMax) {
                v = a > b ? a : b;
                if (c > v) {
                    v = c;
                }
            } else {
                v = (a + 2 * b + c) >> 2;
            }
            dp[static_cast<size_t>(i) * step] = static_cast<uint8_t>(v);
        }
    }
}

// Byte-identical to SleepAnimation::buildScrim, minus the Overlay struct
// indirection (src/tmp/out/haloRuns/haloN are the plain arrays that struct's
// fields point at) and the SleepAnimation-only run-widening of the glyph
// span tables, which buildScrim itself does not touch (that happens in
// publishOverlayRanges, kernel 1's caller).
void buildScrim_ref(const uint8_t *__restrict src, uint8_t *__restrict tmp, uint8_t *__restrict out, int sw, int sh,
                    int q8, uint32_t *__restrict haloRuns, uint8_t *__restrict haloN) {
    // Dilate: two 3-wide max passes per axis.
    scrimTap3_ref(src, tmp, sh, sw, sw, 1, true);
    scrimTap3_ref(tmp, out, sh, sw, sw, 1, true);
    scrimTap3_ref(out, tmp, sw, sh, 1, sw, true);
    scrimTap3_ref(tmp, out, sw, sh, 1, sw, true);
    // Smooth.
    scrimTap3_ref(out, tmp, sh, sw, sw, 1, false);
    scrimTap3_ref(tmp, out, sw, sh, 1, sw, false);

    for (int cy = 0; cy < sh; cy++) {
        uint8_t *const row = out + static_cast<size_t>(cy) * sw;
        for (int cx = 0; cx < sw; cx++) {
            int dim = (row[cx] * q8) >> 8;
            if (dim > 255) {
                dim = 255;
            }
            row[cx] = static_cast<uint8_t>(SCRIM_INV_NONE - ((dim + 4) >> 3));
        }
        uint32_t *const runs = haloRuns + static_cast<size_t>(cy) * RUNS_PER_ROW;
        int n = 0;
        int start = -1;
        for (int cx = 0; cx < sw; cx++) {
            if (row[cx] != SCRIM_INV_NONE) {
                if (start < 0) {
                    start = cx;
                }
                continue;
            }
            if (start < 0) {
                continue;
            }
            n = emitRun_ref(runs, n, start, cx, HALO_GAP_MERGE_CELLS);
            start = -1;
        }
        if (start >= 0) {
            n = emitRun_ref(runs, n, start, sw, HALO_GAP_MERGE_CELLS);
        }
        haloN[cy] = static_cast<uint8_t>(n);
    }
}

const BuildScrimVariant kBuildScrimVariants[] = {
    {"ref_scalar", &buildScrim_ref, true, false},
};
const int kBuildScrimVariantCount = sizeof(kBuildScrimVariants) / sizeof(kBuildScrimVariants[0]);

} // namespace ovb
