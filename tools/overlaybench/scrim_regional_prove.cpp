// Bit-exactness proof for buildScrim_regional against buildScrim_ref.
//
// Property under test: starting from the completed state of a full build
// over src0, mutating src rows [r0, r1] into src1 and running the regional
// rebuild yields exactly the state a full build over src1 produces: the
// inverted dim grid byte-for-byte, haloN per row, and each row's used run
// prefix. Randomized grids and bands plus the directed edge cases the
// margin argument leans on (bands at the grid edges, single-row bands,
// whole-grid bands, mutations that only add or only remove coverage).
//
// Build and run:
//   g++ -O2 -std=c++17 -I. scrim_regional_prove.cpp kernels/scrim_build.cpp \
//       -o build/scrim_regional_prove && ./build/scrim_regional_prove
#include "kernels/scrim_build.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

using namespace ovb;

namespace {

constexpr int SW = SCRIM_W;
constexpr int SH = SCRIM_H;
constexpr size_t CELLS = static_cast<size_t>(SW) * SH;

uint8_t src0[CELLS], src1[CELLS];
uint8_t tmpA[CELLS], tmpB[CELLS];
uint8_t outRef[CELLS], outReg[CELLS];
uint32_t runsRef[SH * RUNS_PER_ROW], runsReg[SH * RUNS_PER_ROW];
uint8_t nRef[SH], nReg[SH];

std::mt19937 rng(0x5ca1ab1e);

// Sparse alpha blobs, the shape scrimSrc actually carries (glyph coverage
// peaks): mostly zero, a few rectangles of high alpha with soft noise.
void randomGrid(uint8_t *g) {
    std::memset(g, 0, CELLS);
    const int blobs = static_cast<int>(rng() % 6);
    for (int b = 0; b < blobs; b++) {
        const int x0 = static_cast<int>(rng() % SW);
        const int y0 = static_cast<int>(rng() % SH);
        const int w = 1 + static_cast<int>(rng() % 24);
        const int h = 1 + static_cast<int>(rng() % 10);
        for (int y = y0; y < y0 + h && y < SH; y++) {
            for (int x = x0; x < x0 + w && x < SW; x++) {
                g[static_cast<size_t>(y) * SW + x] = static_cast<uint8_t>(128 + (rng() % 128));
            }
        }
    }
}

void mutateRows(uint8_t *g, int r0, int r1, int mode) {
    for (int y = r0; y <= r1; y++) {
        uint8_t *row = g + static_cast<size_t>(y) * SW;
        switch (mode) {
        case 0: // clear: coverage disappears (widget moved away)
            std::memset(row, 0, SW);
            break;
        case 1: // fresh random content
            for (int x = 0; x < SW; x++) {
                row[x] = (rng() % 4 == 0) ? static_cast<uint8_t>(rng() % 256) : 0;
            }
            break;
        default: // sparse pokes
            for (int p = 0; p < 8; p++) {
                row[rng() % SW] = static_cast<uint8_t>(rng() % 256);
            }
            break;
        }
    }
}

bool compareState(int q8, int r0, int r1, int iter) {
    if (std::memcmp(outRef, outReg, CELLS) != 0) {
        for (size_t i = 0; i < CELLS; i++) {
            if (outRef[i] != outReg[i]) {
                std::printf("FAIL iter=%d q8=%d band=[%d,%d]: out differs at cell (%zu,%zu): ref=%u reg=%u\n", iter,
                            q8, r0, r1, i % SW, i / SW, outRef[i], outReg[i]);
                return false;
            }
        }
    }
    for (int y = 0; y < SH; y++) {
        if (nRef[y] != nReg[y]) {
            std::printf("FAIL iter=%d q8=%d band=[%d,%d]: haloN[%d] ref=%u reg=%u\n", iter, q8, r0, r1, y, nRef[y],
                        nReg[y]);
            return false;
        }
        if (std::memcmp(runsRef + static_cast<size_t>(y) * RUNS_PER_ROW, runsReg + static_cast<size_t>(y) * RUNS_PER_ROW,
                        static_cast<size_t>(nRef[y]) * 4) != 0) {
            std::printf("FAIL iter=%d q8=%d band=[%d,%d]: runs row %d differ\n", iter, q8, r0, r1, y);
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    int tested = 0;
    for (int iter = 0; iter < 20000; iter++) {
        const int q8 = (iter % 5 == 0) ? 256 : static_cast<int>(rng() % 257);
        randomGrid(src0);

        // Directed edge-case bands first, then random ones.
        int r0, r1;
        switch (iter % 7) {
        case 0:
            r0 = 0;
            r1 = static_cast<int>(rng() % 8);
            break; // top edge
        case 1:
            r1 = SH - 1;
            r0 = SH - 1 - static_cast<int>(rng() % 8);
            break; // bottom edge
        case 2:
            r0 = r1 = static_cast<int>(rng() % SH);
            break; // single row
        case 3:
            r0 = 0;
            r1 = SH - 1;
            break; // whole grid
        default:
            r0 = static_cast<int>(rng() % SH);
            r1 = r0 + static_cast<int>(rng() % 20);
            if (r1 >= SH) {
                r1 = SH - 1;
            }
            break;
        }

        // State of the previous full build over src0.
        buildScrim_ref(src0, tmpA, outReg, SW, SH, q8, runsReg, nReg);

        // Mutate only [r0, r1], regional-update the state.
        std::memcpy(src1, src0, CELLS);
        mutateRows(src1, r0, r1, iter % 3);
        buildScrim_regional(src1, tmpA, tmpB, outReg, SW, SH, q8, runsReg, nReg, r0, r1);

        // Reference: full build over src1.
        buildScrim_ref(src1, tmpA, outRef, SW, SH, q8, runsRef, nRef);

        if (!compareState(q8, r0, r1, iter)) {
            return 1;
        }
        tested++;
    }
    // Phase 2: two changed bands per update, rebuilt by two sequential
    // regional calls (the shape the publish site actually uses: one call per
    // changed scan range). Includes bands close enough that their rewrite
    // windows overlap or abut.
    for (int iter = 0; iter < 20000; iter++) {
        const int q8 = static_cast<int>(rng() % 257);
        randomGrid(src0);
        int a0 = static_cast<int>(rng() % SH);
        int a1 = a0 + static_cast<int>(rng() % 8);
        if (a1 >= SH) {
            a1 = SH - 1;
        }
        int gap = static_cast<int>(rng() % 16); // 0..15: from abutting to well separated
        int b0 = a1 + 1 + gap;
        int b1 = b0 + static_cast<int>(rng() % 8);
        if (b0 >= SH) {
            b0 = SH - 1;
        }
        if (b1 >= SH) {
            b1 = SH - 1;
        }

        buildScrim_ref(src0, tmpA, outReg, SW, SH, q8, runsReg, nReg);
        std::memcpy(src1, src0, CELLS);
        mutateRows(src1, a0, a1, iter % 3);
        mutateRows(src1, b0, b1, (iter + 1) % 3);
        buildScrim_regional(src1, tmpA, tmpB, outReg, SW, SH, q8, runsReg, nReg, a0, a1);
        buildScrim_regional(src1, tmpA, tmpB, outReg, SW, SH, q8, runsReg, nReg, b0, b1);
        buildScrim_ref(src1, tmpA, outRef, SW, SH, q8, runsRef, nRef);
        if (!compareState(q8, a0, b1, 100000 + iter)) {
            return 1;
        }
        tested++;
    }
    std::printf("OK: %d randomized regional rebuilds bit-exact against full builds\n", tested);
    return 0;
}
