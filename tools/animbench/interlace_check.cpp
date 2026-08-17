// Checks the band() call-shape invariant that SleepAnimation's interlaced
// half-res path depends on: a row must come out identical whether it is
// rendered as part of a multi-row band, as a solitary rows==1 call, or as a
// solitary rows==1 call in a parity-skipping sequence that never visits y-1
// first. An optimization that caches derived rows across band() calls can break
// one of those shapes while leaving the ordinary full-band render -- and so the
// golden-frame comparison in bench.cpp -- perfectly bit-exact.
//
// Method: one init() per animation, then per timestep ONE frame() call followed
// by the same frame re-rendered in every shape. band() is expected to be pure
// with respect to animation state (it may keep scratch, but must not advance
// anything), so all shapes must agree, and rendering the reference shape twice
// is the control that proves that purity.
//
// Rendering after a single frame() is what makes this work on all 13
// animations. The obvious alternative -- give each shape a fresh
// release()+init() -- cannot check starfield, fireflies, steam or nebula at
// all, because their state is not reproducible across init() (a re-seeded
// particle field), so two identical renders already differ and the comparison
// says nothing about band(). Holding one init() sidesteps reproducibility
// entirely: the animations that used to report N/A are covered here.
//
// Band heights: every shape here, the reference above all, must satisfy the
// preconditions band() actually imposes. AnimOrbits bins its path points into
// fixed 16-row bins and reads exactly one bin per call, so a band straddling a
// bin boundary loses path pixels. Heights dividing 16 are safe; others are not.
//
// That rules out the shape it is most tempting to use as the reference: a
// single whole-frame call. For orbits that is y0 == 0, rows == 480, so
// bandIdx == 0 and ONLY the first bin's path points are drawn -- the
// whole-frame render is the broken one, and using it as the reference reports
// every legitimate shape as a mismatch (measured: 1422 px, in the 16-row
// column, against a correct 16-row render). The reference is production's
// 8-row band instead, which is a shape the device genuinely uses and which
// every animation supports. Whole-frame is not tested because nothing calls
// band() that way.
//
// Build:
//   g++ -O2 -std=gnu++17 -Ishim -I. -include mathcount.h interlace_check.cpp
//       mathcount.cpp ../../src/display/ui/default/bganim/*.cpp
//       -o build/interlace_check -lm
// Usage: ./build/interlace_check [width] [height]
#include "../../src/display/ui/default/bganim/BgAnim.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

struct Shape {
    const char *name;
    int bandH;  // rows per call; H means one whole-frame call
    int parity; // -1 = contiguous, else render only rows of this parity, rows==1
};

// Renders one already-advanced frame in the given shape. Never calls frame().
void renderShape(const BgAnimation &anim, const Shape &s, int W, int H, const uint8_t p[4], uint16_t *fb) {
    if (s.parity < 0) {
        const int bandH = (s.bandH > H) ? H : s.bandH;
        for (int y = 0; y < H; y += bandH) {
            const int rows = (y + bandH <= H) ? bandH : (H - y);
            anim.band(fb + static_cast<size_t>(y) * W, y, rows, W, 0, p);
        }
    } else {
        for (int y = s.parity; y < H; y += 2) {
            anim.band(fb + static_cast<size_t>(y) * W, y, 1, W, 0, p);
        }
    }
}

// Counts differing pixels over the rows this shape actually wrote.
long compare(const uint16_t *ref, const uint16_t *got, const Shape &s, int W, int H) {
    const int step = (s.parity < 0) ? 1 : 2;
    const int first = (s.parity < 0) ? 0 : s.parity;
    long bad = 0;
    for (int y = first; y < H; y += step) {
        for (int x = 0; x < W; x++) {
            const size_t i = static_cast<size_t>(y) * W + x;
            if (ref[i] != got[i]) {
                bad++;
            }
        }
    }
    return bad;
}

} // namespace

int main(int argc, char **argv) {
    const int W = argc > 1 ? atoi(argv[1]) : 480;
    const int H = argc > 2 ? atoi(argv[2]) : 480;
    const uint32_t times[] = {1000, 2000, 5000, 40000, 999999};
    const int nTimes = static_cast<int>(sizeof(times) / sizeof(times[0]));

    // Shape 0 is the reference: SleepAnimation's own 8-row band. Shape 1
    // repeats it as the band()-purity control. 4 and 16 also divide 16 (see the
    // note above on why every height must); 1 is the interlaced path's shape,
    // and the parity pair is that path exactly.
    const Shape shapes[] = {
        {"8-row", 8, -1},  {"8-row(ctl)", 8, -1}, {"16-row", 16, -1}, {"4-row", 4, -1},
        {"rows==1", 1, -1}, {"parity0", 1, 0},    {"parity1", 1, 1},
    };
    const int nS = static_cast<int>(sizeof(shapes) / sizeof(shapes[0]));
    const size_t frameSz = static_cast<size_t>(W) * H;

    printf("%dx%d, %d timesteps, %d shapes (reference: %s)\n\n", W, H, nTimes, nS - 1, shapes[0].name);
    int failures = 0;
    for (int id = 0; id < bg_animation_count(); id++) {
        const BgAnimation &anim = bg_animation(id);
        uint8_t p[4];
        bg_parse_params(nullptr, id, p);
        if (anim.release != nullptr) {
            anim.release();
        }
        if (!anim.init(W, H)) {
            printf("%-10s SKIP (init failed)\n", anim.id);
            continue;
        }

        std::vector<uint16_t> bufs(frameSz * nS);
        long worst = 0;
        const char *worstShape = "";
        long ctl = 0;
        for (int f = 0; f < nTimes; f++) {
            anim.frame(times[f], W, H, p); // one advance; every shape renders THIS state
            for (int s = 0; s < nS; s++) {
                renderShape(anim, shapes[s], W, H, p, bufs.data() + frameSz * s);
            }
            for (int s = 1; s < nS; s++) {
                const long bad = compare(bufs.data(), bufs.data() + frameSz * s, shapes[s], W, H);
                if (s == 1) {
                    ctl += bad;
                }
                if (bad > worst) {
                    worst = bad;
                    worstShape = shapes[s].name;
                }
            }
        }
        // A nonzero control means band() itself is not pure -- it advanced or
        // consumed state -- which invalidates every other column, so say that
        // rather than blaming a shape.
        if (ctl != 0) {
            printf("%-10s IMPURE   band() not idempotent (control diff %ld)\n", anim.id, ctl);
            failures++;
            continue;
        }
        printf("%-10s %s", anim.id, worst == 0 ? "OK" : "MISMATCH");
        if (worst != 0) {
            printf("  worst %ld px in %s", worst, worstShape);
            failures++;
        }
        printf("\n");
    }
    printf("\n%s (%d failing)\n", failures == 0 ? "ALL OK" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
