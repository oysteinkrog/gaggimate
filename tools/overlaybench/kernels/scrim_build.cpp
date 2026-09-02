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

namespace {

// Scratch for the transpose-based variants below: a transpose needs
// somewhere to hold the grid rotated into the other orientation, and the
// tmp/out buffers the caller provides are only sized for the "normal"
// ping-pong (they get reused as-is for the passes that stay normal). Sized
// for the one shape this kernel is ever called with (SCRIM_W x SCRIM_H, 120
// x 120, both from common.h); buildScrim_transposed34/_all fall back to
// buildScrim_ref rather than overrun a fixed buffer if that ever changes.
// Static, not thread-local or re-allocated per call: buildScrim runs on the
// UI task only, never reentrantly, matching the real firmware's usage, and
// a host benchmark that malloc'd scratch inside the timed function would
// misattribute allocator cost to the kernel.
constexpr size_t kMaxScrimCells = static_cast<size_t>(SCRIM_W) * SCRIM_H;
uint8_t g_transposeA[kMaxScrimCells];
uint8_t g_transposeB[kMaxScrimCells];

// Transpose an n x n grid: dst[x][y] = src[y][x]. This kernel is always
// square (SCRIM_W == SCRIM_H == 120); callers assert that and fall back to
// buildScrim_ref rather than silently mis-transpose a rectangular grid.
//
// Blocked so the "transposing" itself never touches src/dst with a stride:
// each tile is read as kBlock sequential runs of up to kBlock bytes, held in
// a tiny on-stack buffer (kBlock*kBlock bytes -- trivially cache-resident on
// any architecture), and the transpose happens only inside that local
// buffer; the tile is then written out as kBlock more sequential runs. A
// naive element-at-a-time transpose (dst[x*n+y] = src[y*n+x] in a plain
// double loop) would just move the 120-byte stride from the caller's V-pass
// onto the transpose's write side instead of removing it -- the local tile
// is what actually avoids that.
//
// always_inline, not plain inline: same reasoning as common.h's
// scale565_ref/blend565_ref/etc -- at -O2 GCC has repeatedly declined to
// inline same-file helpers in this codebase (confirmed here too: without
// the attribute, this and emitHaloFromOut below showed up as real
// out-of-line calls in the asm dump, unlike buildScrim_ref's six fully
// inlined scrimTap3_ref calls -- see the report for the before/after
// instruction counts).
__attribute__((always_inline)) inline void transposeSquare(const uint8_t *__restrict src, uint8_t *__restrict dst,
                                                            int n) {
    constexpr int kBlock = 16;
    uint8_t tile[kBlock][kBlock];
    for (int by = 0; by < n; by += kBlock) {
        const int yEnd = (by + kBlock < n) ? by + kBlock : n;
        const int bh = yEnd - by;
        for (int bx = 0; bx < n; bx += kBlock) {
            const int xEnd = (bx + kBlock < n) ? bx + kBlock : n;
            const int bw = xEnd - bx;
            // Sequential read: bh rows of bw contiguous source bytes each.
            for (int y = 0; y < bh; y++) {
                const uint8_t *srow = src + static_cast<size_t>(by + y) * n + bx;
                for (int x = 0; x < bw; x++) {
                    tile[y][x] = srow[x];
                }
            }
            // Sequential write: bw rows of bh contiguous destination bytes
            // each. The only stride left (tile[y][x] for fixed x, varying
            // y) is a 16-byte stride inside a 256-byte on-stack array, not a
            // 120-byte stride inside a 14.4 KB PSRAM buffer.
            for (int x = 0; x < bw; x++) {
                uint8_t *drow = dst + static_cast<size_t>(bx + x) * n + by;
                for (int y = 0; y < bh; y++) {
                    drow[y] = tile[y][x];
                }
            }
        }
    }
}

// Shared by both transposed variants: the two horizontal (already
// sequential) dilate passes, unchanged from buildScrim_ref. always_inline for
// the same reason as transposeSquare above.
__attribute__((always_inline)) inline void dilateHTwice(const uint8_t *__restrict src, uint8_t *__restrict tmp,
                                                        uint8_t *__restrict out, int sw, int sh) {
    scrimTap3_ref(src, tmp, sh, sw, sw, 1, true);
    scrimTap3_ref(tmp, out, sh, sw, sw, 1, true);
}

// Shared by both transposed variants: the halo run emission, byte-for-byte
// the loop in buildScrim_ref, over whatever `out` holds after the six passes.
// always_inline for the same reason as transposeSquare above.
__attribute__((always_inline)) inline void emitHaloFromOut(uint8_t *__restrict out, int sw, int sh, int q8,
                                                            uint32_t *__restrict haloRuns,
                                                            uint8_t *__restrict haloN) {
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

} // namespace

// See scrim_build.h. Parameter-mapping proof for why this is bit-exact,
// not just empirically matching the golden:
//
// buildScrim_ref's pass 3 is scrimTap3_ref(out, tmp, sw, sh, 1, sw, true) --
// lines=sw, n=sh, lineStep=1, step=sw. That call treats `out` as sh rows of
// sw columns and, for each column x (0..sw-1, the "lines"), does a 3-tap
// max over rows y (0..sh-1, the "n"-indexed dimension, clamped at 0/sh-1).
//
// transposeSquare(out, t1, n) with n=sw=sh puts t1[x][y] = out[y][x] in a
// row-major-width-n layout. The same per-column operation is now, for each
// row x of t1 (0..sw-1 = "lines"), a sequential 3-tap max over t1's columns
// y (0..sh-1 = "n", same clamp bound because n=sh is unchanged) -- i.e.
// exactly scrimTap3_ref(t1, t2, sw, sh, n, 1, true): lines=sw, n=sh,
// lineStep=n (stride between t1's rows), step=1 (sequential within a row).
// Same reasoning gives pass 4's transposed form. transposeSquare(t1, out, n)
// then undoes the rotation, so `out` holds exactly what buildScrim_ref's
// pass 4 would have left there. Passes 5 and 6 are untouched (literally the
// same calls, on the same tmp/out buffers, as buildScrim_ref), so they run
// on provably-identical input and produce identical output.
void buildScrim_transposed34(const uint8_t *__restrict src, uint8_t *__restrict tmp, uint8_t *__restrict out, int sw,
                             int sh, int q8, uint32_t *__restrict haloRuns, uint8_t *__restrict haloN) {
    if (sw != sh || static_cast<size_t>(sw) * sh > kMaxScrimCells) {
        buildScrim_ref(src, tmp, out, sw, sh, q8, haloRuns, haloN);
        return;
    }
    const int n = sw;
    uint8_t *const t1 = g_transposeA;
    uint8_t *const t2 = g_transposeB;

    // Dilate H, H -- unchanged, normal orientation, already sequential.
    dilateHTwice(src, tmp, out, sw, sh);

    // Dilate V, V -- transpose once, run both as sequential calls on the
    // transposed grid, transpose back once.
    transposeSquare(out, t1, n);
    scrimTap3_ref(t1, t2, n, n, n, 1, true);
    scrimTap3_ref(t2, t1, n, n, n, 1, true);
    transposeSquare(t1, out, n);

    // Smooth H -- unchanged, normal, sequential.
    scrimTap3_ref(out, tmp, sh, sw, sw, 1, false);
    // Smooth V -- left exactly as buildScrim_ref computes it: one isolated
    // strided pass, not bracketed with its own transpose (see report).
    scrimTap3_ref(tmp, out, sw, sh, 1, sw, false);

    emitHaloFromOut(out, sw, sh, q8, haloRuns, haloN);
}

// buildScrim_transposed34, but pass 6 gets the same treatment as passes 3/4:
// transpose in, run sequential, transpose out. Between pass 4's transpose-
// out and pass 6's transpose-in sits pass 5, which must run in NORMAL
// orientation (it is horizontal-shaped; running it on transposed data would
// make IT the strided pass instead, not eliminate a strided pass -- see the
// report for why grouping all three vertical passes into one transposed span
// does not work, only pairing adjacent same-shape passes does). So this
// variant pays for two independent transpose brackets: one around passes
// 3-4 (as above) and a second, separate one around pass 6 alone.
void buildScrim_transposed_all(const uint8_t *__restrict src, uint8_t *__restrict tmp, uint8_t *__restrict out,
                               int sw, int sh, int q8, uint32_t *__restrict haloRuns, uint8_t *__restrict haloN) {
    if (sw != sh || static_cast<size_t>(sw) * sh > kMaxScrimCells) {
        buildScrim_ref(src, tmp, out, sw, sh, q8, haloRuns, haloN);
        return;
    }
    const int n = sw;
    uint8_t *const t1 = g_transposeA;
    uint8_t *const t2 = g_transposeB;

    dilateHTwice(src, tmp, out, sw, sh);

    transposeSquare(out, t1, n);
    scrimTap3_ref(t1, t2, n, n, n, 1, true);
    scrimTap3_ref(t2, t1, n, n, n, 1, true);
    transposeSquare(t1, out, n);

    scrimTap3_ref(out, tmp, sh, sw, sw, 1, false);

    // Smooth V (pass 6), transposed instead of strided: pass 6 is
    // scrimTap3_ref(tmp, out, sw, sh, 1, sw, false) -- lines=sw, n=sh,
    // lineStep=1, step=sw. transposeSquare(tmp, t1, n) puts t1[x][y] =
    // tmp[y][x], so the per-column smooth becomes
    // scrimTap3_ref(t1, t2, sw, sh, n, 1, false): lines=sw, n=sh, lineStep=n,
    // step=1 -- same parameter-mapping argument as pass 3/4 above. Then
    // transposeSquare(t2, out, n) restores normal orientation into `out`,
    // matching the contract (halo emission reads `out` in normal layout).
    transposeSquare(tmp, t1, n);
    scrimTap3_ref(t1, t2, n, n, n, 1, false);
    transposeSquare(t2, out, n);

    emitHaloFromOut(out, sw, sh, q8, haloRuns, haloN);
}

void buildScrim_regional(const uint8_t *__restrict src, uint8_t *__restrict tmp, uint8_t *__restrict tmp2,
                         uint8_t *__restrict out, int sw, int sh, int q8, uint32_t *__restrict haloRuns,
                         uint8_t *__restrict haloN, int r0, int r1) {
    if (r0 < 0) {
        r0 = 0;
    }
    if (r1 >= sh) {
        r1 = sh - 1;
    }
    if (r1 < r0) {
        return;
    }
    // Rows whose final value can differ (vertical reach 3, see the header).
    const int w0 = r0 - 3 < 0 ? 0 : r0 - 3;
    const int w1 = r1 + 3 >= sh ? sh - 1 : r1 + 3;
    // Input band: 3 more rows so band-edge clamping stays out of [w0, w1].
    const int b0 = r0 - 6 < 0 ? 0 : r0 - 6;
    const int b1 = r1 + 6 >= sh ? sh - 1 : r1 + 6;
    const int bl = b1 - b0 + 1;
    const size_t off = static_cast<size_t>(b0) * sw;

    // The six passes, band-local. Horizontal passes are exact per row; the
    // vertical passes clamp at the band edges, which is either the grid edge
    // (reference behaviour) or at least 3 rows away from [w0, w1].
    scrimTap3_ref(src + off, tmp + off, bl, sw, sw, 1, true);
    scrimTap3_ref(tmp + off, tmp2 + off, bl, sw, sw, 1, true);
    scrimTap3_ref(tmp2 + off, tmp + off, sw, bl, 1, sw, true);
    scrimTap3_ref(tmp + off, tmp2 + off, sw, bl, 1, sw, true);
    scrimTap3_ref(tmp2 + off, tmp + off, bl, sw, sw, 1, false);
    scrimTap3_ref(tmp + off, tmp2 + off, sw, bl, 1, sw, false);

    // Quantize + emit halo runs, rewrite window only. Byte-for-byte the loop
    // in buildScrim_ref, reading the band result and writing `out` in place.
    for (int cy = w0; cy <= w1; cy++) {
        const uint8_t *const brow = tmp2 + static_cast<size_t>(cy) * sw;
        uint8_t *const row = out + static_cast<size_t>(cy) * sw;
        for (int cx = 0; cx < sw; cx++) {
            int dim = (brow[cx] * q8) >> 8;
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
    {"transposed34", &buildScrim_transposed34, true, false},
    {"transposed_all", &buildScrim_transposed_all, true, false},
};
const int kBuildScrimVariantCount = sizeof(kBuildScrimVariants) / sizeof(kBuildScrimVariants[0]);

} // namespace ovb
