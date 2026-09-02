// Layer 2 bit-exactness proof: blendRow_group8_model vs. blendRow_ref (the
// FULL function -- run walking included, not just the per-pixel formula),
// modeled on tools/overlaybench/scrim_regional_prove.cpp's style
// (randomized inputs, directed edge cases first, a byte-for-byte
// compareState-style diff with a clear FAIL report naming the iteration
// and position).
//
// Property under test: for the same (dst, colour, runs) input, running
// blendRow_group8_model produces a row byte-identical to blendRow_ref,
// across run alignments that deliberately straddle 8-pixel-group
// boundaries and every group-dispatch path (all-opaque copy, no-lane-
// opaque vector-general including a==0 lanes, and mixed-opaque scalar
// fallback).
//
// Build and run:
//   g++ -O2 -std=c++17 -Wall -Wextra -I. prove_layer2.cpp -o build/prove_layer2
//   ./build/prove_layer2
#include "blend_model.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

using namespace blendopt;

namespace {

constexpr int ROW_W = 480; // matches PANEL_W (tools/overlaybench/common.h)
constexpr int MAX_RUNS = 24; // matches RUNS_PER_ROW

std::mt19937 rng(0x5ca1ab1e);

// One row's worth of overlay colour+alpha (3 B/px, matching the real
// LV_IMG_CF_TRUE_COLOR_ALPHA layout blendRow reads) and the packed run
// list blendRow walks.
struct RowInput {
    std::vector<uint8_t> colour; // ROW_W * 3
    std::vector<uint32_t> runs;
    int nRuns = 0;
};

// Alpha mixture, biased toward the three cases that matter for dispatch
// coverage: exactly 0 (skipped pixels), exactly 255 (opaque interior),
// and a spread across [1,254] (antialiased rim, general path).
uint8_t randomAlpha(int mode) {
    switch (mode) {
    case 0:
        return 0;
    case 1:
        return 255;
    default:
        return static_cast<uint8_t>(1 + (rng() % 254));
    }
}

void fillRandomColour(std::vector<uint8_t> &colour) {
    for (int x = 0; x < ROW_W; x++) {
        const uint16_t c = static_cast<uint16_t>(rng() & 0xFFFFu);
        colour[static_cast<size_t>(x) * 3 + 0] = static_cast<uint8_t>(c & 0xFF);
        colour[static_cast<size_t>(x) * 3 + 1] = static_cast<uint8_t>((c >> 8) & 0xFF);
        // Alpha mixture per pixel: this is deliberately re-drawn per run
        // below for the pixels a run actually covers, since untouched
        // pixels' alpha is irrelevant (blendRow never reads outside a
        // run's [x0,xEnd)).
        colour[static_cast<size_t>(x) * 3 + 2] = 0;
    }
}

// Paint one run's pixel range with a chosen alpha "shape": uniform-opaque
// (every lane 255, exercises allOpaque groups), uniform-general (every
// lane in [0,254] with plenty of exact 0s mixed in, exercises the vector
// path including the a==0 identity case), or mixed (roughly half 255, half
// not -- exercises the scalar-fallback group path, including at group
// boundaries so a single group can straddle two runs' worth of shape).
void paintRun(std::vector<uint8_t> &colour, int x0, int x1, int shape) {
    for (int x = x0; x < x1; x++) {
        uint8_t a;
        switch (shape) {
        case 0: // uniform opaque
            a = 255;
            break;
        case 1: // uniform general, including a==0 lanes
            a = randomAlpha((rng() % 3 == 0) ? 0 : 2);
            break;
        default: // mixed opaque/general, worst case for group dispatch
            a = (rng() % 2 == 0) ? 255 : randomAlpha(rng() % 2);
            break;
        }
        colour[static_cast<size_t>(x) * 3 + 2] = a;
        // Give opaque and general lanes distinguishable, non-trivial
        // colour so a copy-vs-blend mixup would show up as a wrong pixel,
        // not a coincidentally-matching one.
        const uint16_t c = static_cast<uint16_t>(rng() & 0xFFFFu);
        colour[static_cast<size_t>(x) * 3 + 0] = static_cast<uint8_t>(c & 0xFF);
        colour[static_cast<size_t>(x) * 3 + 1] = static_cast<uint8_t>((c >> 8) & 0xFF);
    }
}

int emitRun(std::vector<uint32_t> &runs, int n, int x0, int x1) {
    runs[static_cast<size_t>(n)] = static_cast<uint32_t>(x0) | (static_cast<uint32_t>(x1) << 16);
    return n + 1;
}

bool compareRows(const std::vector<uint16_t> &ref, const std::vector<uint16_t> &reg, int iter, const char *label) {
    if (std::memcmp(ref.data(), reg.data(), ref.size() * sizeof(uint16_t)) == 0) {
        return true;
    }
    for (size_t x = 0; x < ref.size(); x++) {
        if (ref[x] != reg[x]) {
            std::printf("FAIL iter=%d [%s]: pixel x=%zu differs: ref=0x%04x reg=0x%04x\n", iter, label, x, ref[x],
                        reg[x]);
            return false;
        }
    }
    return false;
}

} // namespace

int main() {
    long long tested = 0;
    const int ITERS = 50000;

    for (int iter = 0; iter < ITERS; iter++) {
        RowInput in;
        in.colour.assign(static_cast<size_t>(ROW_W) * 3, 0);
        in.runs.assign(MAX_RUNS, 0);
        fillRandomColour(in.colour);

        // Directed edge-case run placements first (iter % 11), then random
        // ones, covering: sub-8-pixel runs, exactly-8-aligned runs,
        // misaligned-start, misaligned-end, runs spanning several groups,
        // a run starting exactly at x=0, a run ending exactly at ROW_W,
        // and adjacent runs whose boundary falls mid-group.
        std::vector<std::pair<int, int>> spans;
        switch (iter % 11) {
        case 0: // one sub-8-pixel run, arbitrary phase
            {
                int x0 = static_cast<int>(rng() % (ROW_W - 8));
                spans.emplace_back(x0, x0 + 1 + static_cast<int>(rng() % 7));
            }
            break;
        case 1: // exactly one aligned 8-pixel group
            {
                int g = static_cast<int>(rng() % (ROW_W / 8));
                spans.emplace_back(g * 8, g * 8 + 8);
            }
            break;
        case 2: // misaligned start, aligned-ish end, spans 2-4 groups
            {
                int x0 = static_cast<int>(rng() % (ROW_W - 40)) + 3; // +3: force misalignment
                spans.emplace_back(x0, x0 + 16 + static_cast<int>(rng() % 16));
            }
            break;
        case 3: // run starting exactly at row start
            spans.emplace_back(0, 5 + static_cast<int>(rng() % 20));
            break;
        case 4: // run ending exactly at row end
            {
                int len = 5 + static_cast<int>(rng() % 20);
                spans.emplace_back(ROW_W - len, ROW_W);
            }
            break;
        case 5: // whole row, one run
            spans.emplace_back(0, ROW_W);
            break;
        case 6: // two adjacent runs whose shared boundary falls mid-group
            {
                int mid = (static_cast<int>(rng() % 50)) * 8 + 3 + static_cast<int>(rng() % 5);
                if (mid < 8) {
                    mid = 8;
                }
                if (mid > ROW_W - 8) {
                    mid = ROW_W - 8;
                }
                spans.emplace_back(mid - 8, mid);
                spans.emplace_back(mid, mid + 8);
            }
            break;
        default: { // several random runs, random alignment, non-overlapping
            int n = 1 + static_cast<int>(rng() % 5);
            int cursor = 0;
            for (int k = 0; k < n && cursor < ROW_W - 4; k++) {
                int gap = static_cast<int>(rng() % 12);
                int x0 = cursor + gap;
                if (x0 >= ROW_W - 2) {
                    break;
                }
                int len = 1 + static_cast<int>(rng() % 30);
                int x1 = x0 + len;
                if (x1 > ROW_W) {
                    x1 = ROW_W;
                }
                spans.emplace_back(x0, x1);
                cursor = x1;
            }
            break;
        }
        }
        if (spans.empty() || static_cast<int>(spans.size()) > MAX_RUNS) {
            continue;
        }

        // Paint each span with a per-iteration shape mixture so, across
        // iterations, all three group-dispatch paths (opaque copy,
        // general vector, mixed scalar fallback) get exercised, including
        // at the misaligned prologue/epilogue pixels.
        const int shape = iter % 3;
        for (auto &sp : spans) {
            paintRun(in.colour, sp.first, sp.second, shape);
        }
        for (size_t k = 0; k < spans.size(); k++) {
            in.nRuns = emitRun(in.runs, in.nRuns, spans[k].first, spans[k].second);
        }

        std::vector<uint16_t> dst0(ROW_W);
        for (int x = 0; x < ROW_W; x++) {
            dst0[static_cast<size_t>(x)] = static_cast<uint16_t>(rng() & 0xFFFFu);
        }
        std::vector<uint16_t> dstRef = dst0;
        std::vector<uint16_t> dstReg = dst0;

        blendRow_ref(dstRef.data(), in.colour.data(), in.runs.data(), in.nRuns);
        blendRow_group8_model(dstReg.data(), in.colour.data(), in.runs.data(), in.nRuns);

        if (!compareRows(dstRef, dstReg, iter, "blendRow_group8_model")) {
            std::printf("LAYER 2: FAIL at iteration %d (nRuns=%d)\n", iter, in.nRuns);
            return 1;
        }
        tested++;
    }

    std::printf("LAYER 2: PASS -- %lld randomized rows (row width %d, up to %d runs/row), "
                "blendRow_group8_model bit-exact vs blendRow_ref\n",
                tested, ROW_W, MAX_RUNS);
    return 0;
}
