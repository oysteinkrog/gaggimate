// Layer 3 semantic-trace proof: executes the EXACT instruction sequence
// parsed live from blend_group8.S (see blend_interp.h) against randomized
// 8-lane (fg, bg, a) groups, and checks every lane against blend565_ref
// (blend_ref.h, verbatim from the real SleepAnimation.cpp).
//
// This is the strongest correctness evidence available without real
// Xtensa execution (no device, no QEMU harness was available for this
// pass -- see the report), but it is NOT equivalent to real execution:
// see the header comment in blend_interp.h for exactly what it does and
// does not prove.
//
// Build and run (must be run from this directory -- it opens
// "blend_group8.S" relative to argv[0]'s directory, defaulting to CWD):
//   g++ -O2 -std=c++17 -Wall -Wextra -I. prove_interp.cpp -o build/prove_interp
//   ./build/prove_interp
#include "blend_interp.h"
#include "blend_model.h"
#include <cstdio>
#include <random>

using namespace blendopt;
using namespace blendopt::interp;

namespace {

long long tested = 0;
long long failed = 0;

void checkLane(const Lane8 &fg, const Lane8 &bg, const Lane8 &a, const Lane8 &out, int iter) {
    for (int i = 0; i < 8; i++) {
        const uint8_t ai = static_cast<uint8_t>(a[static_cast<size_t>(i)]);
        const uint16_t expected = blend565_ref(fg[static_cast<size_t>(i)], bg[static_cast<size_t>(i)], ai);
        const uint16_t actual = out[static_cast<size_t>(i)];
        tested++;
        if (expected != actual) {
            failed++;
            if (failed <= 20) {
                std::printf("FAIL iter=%d lane=%d: fg=0x%04x bg=0x%04x a=%u expected=0x%04x actual=0x%04x\n", iter,
                            i, fg[static_cast<size_t>(i)], bg[static_cast<size_t>(i)], ai, expected, actual);
            }
        }
    }
}

} // namespace

int main() {
    const std::vector<Instr> program = parseAsmFile("blend_group8.S");
    std::printf("parsed %zu instructions from blend_group8.S\n", program.size());

    std::mt19937 rng(0xC0FFEE);
    std::uniform_int_distribution<int> colourDist(0, 65535);
    // a in [0,254] -- 254 (not 255) is the correct upper bound for this
    // path: a==0 is included deliberately (the identity case Layer 1/2's
    // header comments argue for), a==255 is excluded because this path
    // must never receive it (see blend_ref.h / blend_pie_kernel.cpp for
    // the a==255 counterexample).
    std::uniform_int_distribution<int> alphaDist(0, 254);

    const int ITERS = 200000; // 200,000 groups x 8 lanes = 1,600,000 pixel checks

    // Directed groups first: all-a==0 (whole-group identity), all-a==254
    // (near-boundary), then random-mixed groups (the realistic case).
    for (int iter = 0; iter < ITERS; iter++) {
        Lane8 fg{}, bg{}, a{}, inv{};
        int mode = iter % 5;
        for (int i = 0; i < 8; i++) {
            fg[static_cast<size_t>(i)] = static_cast<uint16_t>(colourDist(rng));
            bg[static_cast<size_t>(i)] = static_cast<uint16_t>(colourDist(rng));
            uint8_t av;
            switch (mode) {
            case 0:
                av = 0; // whole group at a==0 -- output must equal bg exactly
                break;
            case 1:
                av = 254; // whole group at the tightest non-opaque boundary
                break;
            case 2:
                av = 1; // whole group at the other boundary
                break;
            default:
                av = static_cast<uint8_t>(alphaDist(rng)); // realistic mixed group, 0..254
                break;
            }
            a[static_cast<size_t>(i)] = av;
            inv[static_cast<size_t>(i)] = static_cast<uint16_t>(256u - av);
        }
        const Lane8 out = runGroup8(program, fg, bg, a, inv);
        checkLane(fg, bg, a, out, iter);
    }

    std::printf("TOTAL lane-checks=%lld failed=%lld\n", tested, failed);
    if (failed != 0) {
        std::printf("LAYER 3 (semantic trace): FAIL\n");
        return 1;
    }
    std::printf("LAYER 3 (semantic trace): PASS -- %lld lane-checks (%d groups x 8 lanes) executing the exact\n"
                "instruction sequence parsed from blend_group8.S, bit-exact vs blend565_ref\n",
                tested, ITERS);
    return 0;
}
