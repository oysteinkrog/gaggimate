// Bit-exactness proof: scrimRow_branchlessCell and scrimRow_branchlessWord
// against scrimRow_ref, over randomized invRow/runs/dst inputs plus the
// directed edge cases the identity argument (see scrimrow.cpp) and the
// word-restructure argument depend on.
//
// Modeled on tools/overlaybench/scrim_regional_prove.cpp's style
// (randomized-input + byte-diff-report), read for the pattern, not copied:
// that file proves a different property (regional vs full rebuild) over a
// different kernel (buildScrim). This is a from-scratch prover for
// scrimRow's own two candidate rewrites.
//
// Build and run:
//   g++ -O2 -std=c++17 -Wall -Wextra prove_scrimfix.cpp scrimrow.cpp
//   -o build/prove_scrimfix && ./build/prove_scrimfix
#include "common.h"
#include "scrimrow.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

using namespace scrimfix;

namespace {

// Panel width used by the real call site (SleepAnimation.cpp: PANEL_W).
// scrimRow's own `w` parameter is in panel pixels, and the loop clamps
// c1 against `w >> SCRIM_SHIFT`, so the caller's `w` must be a multiple of
// 4 for that clamp to be exact -- SleepAnimation.cpp always calls it with
// the real panel width (480, a multiple of 4). The prover also exercises
// `w` values that are NOT multiples of 8/4 (see genCase below, "w not a
// multiple of 8" from the task brief) to make sure the clamp arithmetic
// itself -- shared unmodified between all three variants -- is not hiding
// a divergence.
constexpr int MAX_W = 480;
constexpr int MAX_SCRIM_W = MAX_W >> SCRIM_SHIFT; // 120 cells

std::mt19937 rng(0xb17dfeed);

// One row's worth of scratch: dst is panel pixels (RGB565), invRow is
// scrim-cell factors (0..32), runs is halo cell-runs packed the way the
// real caller packs them (x0 | x1<<16, cell coordinates).
// alignas(4): scrimRow_branchlessWord reinterpret_casts dst to uint32_t*,
// matching the real function's own precondition ("the band is 4-byte
// aligned", SleepAnimation.cpp:207-209, guaranteed there by bandBuf's
// allocation). A misaligned global here would make the reinterpret_cast
// undefined behaviour even though x86 tolerates it silently -- and this
// prover's whole point is to be trustworthy, not just to pass by luck.
alignas(4) uint16_t dstRef[MAX_W], dstCell[MAX_W], dstWord[MAX_W];
uint8_t invRow[MAX_SCRIM_W];

void randomizeDst(uint16_t *d, int w) {
    for (int x = 0; x < w; x++) {
        d[x] = static_cast<uint16_t>(rng());
    }
}

void randomizeInvRow(uint8_t *row, int scrimW, int allNoneChance, int allZeroChance) {
    const bool allNone = allNoneChance > 0 && (rng() % allNoneChance) == 0;
    const bool allZero = !allNone && allZeroChance > 0 && (rng() % allZeroChance) == 0;
    for (int c = 0; c < scrimW; c++) {
        if (allNone) {
            row[c] = SCRIM_INV_NONE;
        } else if (allZero) {
            row[c] = 0;
        } else {
            // 0..32 inclusive -- the real factor's full range (buildScrim's
            // own comment: "stored as 32nds of full brightness").
            row[c] = static_cast<uint8_t>(rng() % (SCRIM_INV_NONE + 1));
        }
    }
}

// Build a run list from a list of [c0,c1) cell ranges, mimicking the shape
// haloRuns actually has: packed uint32_t, x0 in the low 16 bits, x1 in the
// high 16 bits, cell coordinates (not pixel).
int packRuns(uint32_t *runs, const std::vector<std::pair<int, int>> &ranges) {
    int n = 0;
    for (const auto &r : ranges) {
        runs[n++] = static_cast<uint32_t>(r.first) | (static_cast<uint32_t>(r.second) << 16);
    }
    return n;
}

bool compareRow(const char *label, int iter, int w, const uint16_t *ref, const uint16_t *got) {
    for (int x = 0; x < w; x++) {
        if (ref[x] != got[x]) {
            std::printf("FAIL iter=%d [%s] w=%d: pixel %d differs: ref=0x%04x got=0x%04x\n", iter, label, w, x,
                        ref[x], got[x]);
            return false;
        }
    }
    return true;
}

// One test case: run one (invRow, runs, w) input through all three variants
// on freshly-randomized (but identical across variants) dst buffers, then
// compare byte-for-byte.
bool runCase(int iter, int scrimW, int w, const std::vector<std::pair<int, int>> &ranges, int allNoneChance = 8,
            int allZeroChance = 8) {
    randomizeInvRow(invRow, scrimW, allNoneChance, allZeroChance);
    randomizeDst(dstRef, w);
    std::memcpy(dstCell, dstRef, sizeof(uint16_t) * static_cast<size_t>(w));
    std::memcpy(dstWord, dstRef, sizeof(uint16_t) * static_cast<size_t>(w));

    uint32_t runs[64];
    const int nRuns = packRuns(runs, ranges);

    scrimRow_ref(dstRef, invRow, runs, nRuns, w);
    scrimRow_branchlessCell(dstCell, invRow, runs, nRuns, w);
    scrimRow_branchlessWord(dstWord, invRow, runs, nRuns, w);

    bool ok = true;
    ok &= compareRow("branchlessCell", iter, w, dstRef, dstCell);
    ok &= compareRow("branchlessWord", iter, w, dstRef, dstWord);
    return ok;
}

} // namespace

int main() {
    int tested = 0;

    // --- Directed edge cases from the task brief -----------------------

    // nHalo == 0: no runs at all. Both variants must be no-ops, same as ref.
    {
        if (!runCase(1, MAX_SCRIM_W, MAX_W, {})) {
            return 1;
        }
        tested++;
    }

    // A run ending exactly at an odd cell boundary (c1 odd -- the word loop's
    // wordEnd = c1<<1 is then not a multiple of 4 words / not 16-byte
    // aligned; nothing in scrimRow_branchlessWord assumes 16-byte alignment
    // at the END of a run, only that dst itself starts 4-byte aligned, so
    // this is exactly the case that would catch an off-by-one in the word
    // count).
    {
        if (!runCase(2, MAX_SCRIM_W, MAX_W, {{0, 1}})) {
            return 1;
        } // c1=1, odd
        if (!runCase(3, MAX_SCRIM_W, MAX_W, {{2, 5}})) {
            return 1;
        } // c1=5, odd
        if (!runCase(4, MAX_SCRIM_W, MAX_W, {{0, 3}, {5, 7}})) {
            return 1;
        } // both ends odd
        tested += 3;
    }

    // w not a multiple of 8 (nor of 4 in one case): exercises the
    // `if ((c1 << SCRIM_SHIFT) > w) c1 = w >> SCRIM_SHIFT;` clamp, shared
    // unmodified by all three variants, at a variety of remainders.
    for (int w : {479, 477, 473, 468, 465, 461, 1, 2, 3, 4, 5, 7}) {
        const int scrimW = (w + (1 << SCRIM_SHIFT) - 1) >> SCRIM_SHIFT;
        if (!runCase(100 + w, scrimW, w, {{0, scrimW}})) {
            return 1;
        }
        tested++;
    }

    // All-SCRIM_INV_NONE row: the sharpest test of the branchless identity
    // substitution -- every cell in every run takes the "would have been
    // skipped" path in ref, and must come out byte-identical anyway.
    for (int iter = 0; iter < 200; iter++) {
        if (!runCase(1000 + iter, MAX_SCRIM_W, MAX_W, {{0, MAX_SCRIM_W}}, /*allNoneChance=*/1, /*allZeroChance=*/0)) {
            return 1;
        }
        tested++;
    }

    // All-zero row (inv==0, blacks every cell out): the opposite extreme,
    // makes sure the substitution isn't accidentally only correct near the
    // identity factor.
    for (int iter = 0; iter < 200; iter++) {
        if (!runCase(2000 + iter, MAX_SCRIM_W, MAX_W, {{0, MAX_SCRIM_W}}, /*allNoneChance=*/0, /*allZeroChance=*/1)) {
            return 1;
        }
        tested++;
    }

    // --- Randomized general fuzzing -------------------------------------
    for (int iter = 0; iter < 20000; iter++) {
        const int w = 4 + static_cast<int>(rng() % (MAX_W - 3)); // 4..MAX_W, always a valid cell-aligned-ish width
        const int scrimW = (w + (1 << SCRIM_SHIFT) - 1) >> SCRIM_SHIFT;

        // Random run list: a handful of non-overlapping, not-necessarily-
        // contiguous [c0,c1) cell ranges, biased toward the real caller's
        // shape (a handful of runs per row, RUNS_PER_ROW==24 cap upstream --
        // not modeled here since scrimRow itself doesn't cap, its caller
        // does).
        std::vector<std::pair<int, int>> ranges;
        int cursor = 0;
        const int nRanges = 1 + static_cast<int>(rng() % 6);
        for (int r = 0; r < nRanges && cursor < scrimW; r++) {
            const int gap = static_cast<int>(rng() % 4);
            cursor += gap;
            if (cursor >= scrimW) {
                break;
            }
            const int len = 1 + static_cast<int>(rng() % 12);
            int end = cursor + len;
            if (end > scrimW) {
                end = scrimW;
            }
            ranges.emplace_back(cursor, end);
            cursor = end;
        }
        if (!runCase(10000 + iter, scrimW, w, ranges)) {
            return 1;
        }
        tested++;
    }

    std::printf("OK: %d randomized/directed cases, scrimRow_branchlessCell and scrimRow_branchlessWord both "
               "bit-exact against scrimRow_ref\n",
               tested);
    return 0;
}
