// Independent check of the band() call-shape invariant that SleepAnimation's
// interlaced half-res path depends on: band() must produce identical pixels
// whether a row is rendered as part of a multi-row band, as a solitary
// rows==1 call, or as a solitary rows==1 call in a parity-skipping sequence
// that never visits y-1 first.
//
// This is the invariant the comment in AnimNebula.cpp is about, so it is
// checked here rather than trusted: an optimization that caches derived rows
// across band() calls can break one of the three shapes below while leaving
// the ordinary full-band render -- and therefore the golden-frame comparison
// in bench.cpp -- bit-exact.
//
// frame() is stateful for the particle animations, so each shape gets its own
// fresh init() and its own frame() call per timestep. Comparing two sweeps
// taken after a single frame() would instead measure whether band() mutates
// per-frame state, which is not the same question and gives false positives on
// every animation whose frame() advances particles.
//
// The multi-row reference shape must use a band height the animations actually
// support, which means matching SleepAnimation's: AnimOrbits bins its path
// points into 16-row bins and reads exactly one bin per band() call, so a band
// that straddles a bin boundary loses path pixels and the REFERENCE becomes the
// wrong render. A band height of 8 (or any divisor of 16) is safe; 40 is not,
// and reports orbits as a mismatch when the only broken thing is this harness.
//
// Build:
//   g++ -O2 -std=gnu++17 -Ishim -I. -include mathcount.h interlace_check.cpp
//       mathcount.cpp ../../src/display/ui/default/bganim/*.cpp
//       -o build/interlace_check -lm
// Usage: ./build/interlace_check [width] [height] [band_h]
#include "../../src/display/ui/default/bganim/BgAnim.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

enum Shape { SHAPE_BANDS, SHAPE_SINGLE, SHAPE_PARITY0, SHAPE_PARITY1 };

// Renders the whole time sequence in one call shape from a fresh init, and
// returns the frames it produced (one buffer per timestep, concatenated).
std::vector<uint16_t> render(const BgAnimation &anim, Shape shape, int W, int H, int bandH, const uint32_t *times,
                            int nTimes, const uint8_t p[4], bool *ok) {
    const size_t frameSz = static_cast<size_t>(W) * H;
    std::vector<uint16_t> out(frameSz * nTimes, 0);
    *ok = true;
    if (anim.release != nullptr) {
        anim.release();
    }
    if (!anim.init(W, H)) {
        *ok = false;
        return out;
    }
    for (int f = 0; f < nTimes; f++) {
        const uint32_t t = times[f];
        uint16_t *fb = out.data() + frameSz * f;
        anim.frame(t, W, H, p);
        switch (shape) {
        case SHAPE_BANDS:
            for (int y0 = 0; y0 < H; y0 += bandH) {
                const int rows = (y0 + bandH <= H) ? bandH : (H - y0);
                anim.band(fb + static_cast<size_t>(y0) * W, y0, rows, W, t, p);
            }
            break;
        case SHAPE_SINGLE:
            for (int y = 0; y < H; y++) {
                anim.band(fb + static_cast<size_t>(y) * W, y, 1, W, t, p);
            }
            break;
        default: {
            // Only one parity, so each call's predecessor is y-2, and the first
            // call is not row 0 for parity 1. Rows of the other parity are left
            // at 0 and skipped by the comparison.
            const int par = (shape == SHAPE_PARITY0) ? 0 : 1;
            for (int y = par; y < H; y += 2) {
                anim.band(fb + static_cast<size_t>(y) * W, y, 1, W, t, p);
            }
            break;
        }
        }
    }
    return out;
}

// Counts differing pixels, visiting only rows this shape actually wrote.
long compare(const std::vector<uint16_t> &ref, const std::vector<uint16_t> &got, Shape shape, int W, int H, int nTimes) {
    const size_t frameSz = static_cast<size_t>(W) * H;
    const int step = (shape == SHAPE_BANDS || shape == SHAPE_SINGLE) ? 1 : 2;
    const int first = (shape == SHAPE_PARITY1) ? 1 : 0;
    long bad = 0;
    for (int f = 0; f < nTimes; f++) {
        for (int y = first; y < H; y += step) {
            for (int x = 0; x < W; x++) {
                const size_t i = frameSz * f + static_cast<size_t>(y) * W + x;
                if (ref[i] != got[i]) {
                    bad++;
                }
            }
        }
    }
    return bad;
}

} // namespace

int main(int argc, char **argv) {
    const int W = argc > 1 ? atoi(argv[1]) : 240;
    const int H = argc > 2 ? atoi(argv[2]) : 240;
    const int BAND_H = argc > 3 ? atoi(argv[3]) : 8; // SleepAnimation.cpp's value
    if (BAND_H < 1 || (16 % BAND_H) != 0) {
        printf("band height %d is not a divisor of 16; see the note at the top of this file\n", BAND_H);
        return 2;
    }
    const uint32_t times[] = {0, 500, 4000, 30000, 123457};
    const int nTimes = static_cast<int>(sizeof(times) / sizeof(times[0]));

    printf("%dx%d, band height %d, %d frames\n\n", W, H, BAND_H, nTimes);
    int failures = 0, skipped = 0;
    for (int id = 0; id < bg_animation_count(); id++) {
        const BgAnimation &anim = bg_animation(id);
        uint8_t p[4];
        bg_parse_params(nullptr, id, p);

        bool ok = true;
        const std::vector<uint16_t> ref = render(anim, SHAPE_BANDS, W, H, BAND_H, times, nTimes, p, &ok);
        if (!ok) {
            printf("%-10s SKIP (init failed)\n", anim.id);
            skipped++;
            continue;
        }
        // Control: the same shape rendered a second time from a fresh init. Any
        // nonzero count here means this animation's state is not reproducible
        // across init() -- a re-seeded particle field, say -- so the shape
        // comparisons below cannot say anything about band() and are reported as
        // N/A rather than as failures.
        const std::vector<uint16_t> ref2 = render(anim, SHAPE_BANDS, W, H, BAND_H, times, nTimes, p, &ok);
        const long badControl = compare(ref, ref2, SHAPE_BANDS, W, H, nTimes);

        const std::vector<uint16_t> single = render(anim, SHAPE_SINGLE, W, H, BAND_H, times, nTimes, p, &ok);
        const std::vector<uint16_t> par0 = render(anim, SHAPE_PARITY0, W, H, BAND_H, times, nTimes, p, &ok);
        const std::vector<uint16_t> par1 = render(anim, SHAPE_PARITY1, W, H, BAND_H, times, nTimes, p, &ok);

        const long badSingle = compare(ref, single, SHAPE_SINGLE, W, H, nTimes);
        const long badPar = compare(ref, par0, SHAPE_PARITY0, W, H, nTimes) + compare(ref, par1, SHAPE_PARITY1, W, H, nTimes);
        if (badControl != 0) {
            printf("%-10s N/A       not reproducible across init() (control diff %ld)\n", anim.id, badControl);
            skipped++;
            continue;
        }
        const bool pass = badSingle == 0 && badPar == 0;
        printf("%-10s rows==1: %-9ld parity-skip: %-9ld %s\n", anim.id, badSingle, badPar, pass ? "OK" : "MISMATCH");
        if (!pass) {
            failures++;
        }
    }
    printf("\n%s (%d mismatch, %d skipped)\n", failures == 0 ? "ALL OK" : "FAILURES", failures, skipped);
    return failures == 0 ? 0 : 1;
}
