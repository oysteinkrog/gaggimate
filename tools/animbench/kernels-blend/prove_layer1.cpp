// Layer 1 bit-exactness proof: blendPixelGeneral_model vs. blend565_ref
// (verbatim copy of the real blend565, blend_ref.h), over
//   - an exhaustive boundary subset: a in {1,2,127,128,253,254} crossed
//     with a full sweep of fg/bg RGB565 values that exercise every raw
//     channel magnitude combination (not literally all 65536*65536 colour
//     pairs -- see below for the exhaustive-enough construction used
//     instead, plus a large uniformly-random top-up), and
//   - >=100,000 uniformly random (fg, bg, a) triples with a in [1,254].
// NEVER a==0 or a==255 -- both are handled outside this function (see
// blend_model.h's header comment).
//
// Build and run:
//   g++ -O2 -std=c++17 -Wall -Wextra -I. prove_layer1.cpp -o build/prove_layer1
//   ./build/prove_layer1
#include "blend_model.h"
#include <cstdio>
#include <cstdlib>
#include <random>

using namespace blendopt;

namespace {

long long tested = 0;
long long failed = 0;

void check(uint16_t fg, uint16_t bg, uint32_t a, const char *label) {
    const uint16_t expected = blend565_ref(fg, bg, static_cast<uint8_t>(a));
    const uint16_t actual = blendPixelGeneral_model(fg, bg, a);
    tested++;
    if (expected != actual) {
        failed++;
        if (failed <= 20) {
            std::printf("FAIL[%s] fg=0x%04x bg=0x%04x a=%u: expected=0x%04x actual=0x%04x\n", label, fg, bg, a,
                        expected, actual);
        }
    }
}

} // namespace

int main() {
    const int boundaryAlphas[] = {1, 2, 127, 128, 253, 254};

    // Exhaustive-enough colour construction: every one of the 32 raw R
    // values, 64 raw G values, and 32 raw B values appears in isolation
    // (other two channels held at every combination of {0, mid, max}) for
    // both fg and bg, crossed with every boundary alpha. This exercises
    // every channel magnitude the formula's per-channel arithmetic can
    // produce, not just a handful of colours -- the three channels are
    // independent per the derivation in blend_model.h, so varying one
    // channel exhaustively while the others take a representative spread
    // is what "exhaustive over the part of the space that matters" means
    // here, rather than a literal (impractical) 2^32 x 6 sweep.
    std::mt19937 rng(0xB1E5D0u);
    std::uniform_int_distribution<int> chanR(0, 31), chanG(0, 63), chanB(0, 31);
    long long boundaryCount = 0;
    for (int a : boundaryAlphas) {
        for (int rv = 0; rv < 32; rv++) {
            for (int gv = 0; gv < 64; gv++) {
                for (int bv = 0; bv < 32; bv++) {
                    // Full (r,g,b) cross for fg with bg fixed at a small
                    // representative set, and vice versa, rather than the
                    // full 32*64*32 squared (67M) -- still exhaustive over
                    // every single-channel magnitude for both fg and bg.
                    const uint16_t fg = static_cast<uint16_t>((rv << 11) | (gv << 5) | bv);
                    for (int bgv : {0, 15, 31}) {
                        for (int bgg : {0, 31, 63}) {
                            for (int bgb : {0, 15, 31}) {
                                const uint16_t bg = static_cast<uint16_t>((bgv << 11) | (bgg << 5) | bgb);
                                check(fg, bg, static_cast<uint32_t>(a), "boundary-fg-sweep");
                                boundaryCount++;
                            }
                        }
                    }
                }
            }
        }
    }
    // Symmetric pass with bg swept exhaustively and fg from a representative
    // set, so both operands get full-magnitude coverage.
    for (int a : boundaryAlphas) {
        for (int rv = 0; rv < 32; rv++) {
            for (int gv = 0; gv < 64; gv++) {
                for (int bv = 0; bv < 32; bv++) {
                    const uint16_t bg = static_cast<uint16_t>((rv << 11) | (gv << 5) | bv);
                    for (int fgv : {0, 15, 31}) {
                        for (int fgg : {0, 31, 63}) {
                            for (int fgb : {0, 15, 31}) {
                                const uint16_t fg = static_cast<uint16_t>((fgv << 11) | (fgg << 5) | fgb);
                                check(fg, bg, static_cast<uint32_t>(a), "boundary-bg-sweep");
                                boundaryCount++;
                            }
                        }
                    }
                }
            }
        }
    }
    std::printf("boundary sweep: %lld triples (a in {1,2,127,128,253,254}, every raw channel magnitude covered "
                "for both fg and bg)\n",
                boundaryCount);

    // Large random sample, full space: fg, bg uniformly random RGB565
    // colours, a uniformly random in [1,254].
    std::uniform_int_distribution<int> colourDist(0, 65535);
    std::uniform_int_distribution<int> alphaDist(1, 254);
    const long long randomCount = 2000000; // well over the >=100,000 floor
    for (long long i = 0; i < randomCount; i++) {
        const uint16_t fg = static_cast<uint16_t>(colourDist(rng));
        const uint16_t bg = static_cast<uint16_t>(colourDist(rng));
        const uint32_t a = static_cast<uint32_t>(alphaDist(rng));
        check(fg, bg, a, "random");
    }
    std::printf("random sample: %lld triples (fg,bg uniform over all 65536 colours, a uniform in [1,254])\n",
                randomCount);

    std::printf("TOTAL tested=%lld failed=%lld\n", tested, failed);
    if (failed != 0) {
        std::printf("LAYER 1: FAIL\n");
        return 1;
    }
    std::printf("LAYER 1: PASS (blendPixelGeneral_model bit-exact vs blend565_ref over %lld triples)\n", tested);
    return 0;
}
