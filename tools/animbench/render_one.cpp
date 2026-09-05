// render_one: render one animation descriptor, registered or not, to PPM
// frames on the host. The design rounds need frames of a candidate that is
// not (yet) in BgAnimRegistry.cpp, and four workers editing the registry in
// parallel would collide, so the descriptor is named at compile time:
//
//   make -f Makefile.render CAND=bg_anim_mandala2     -> build/render_bg_anim_mandala2
//   ./build/render_bg_anim_mandala2 OUTDIR [frame ...]  (default 30 120 210)
//   ./build/render_bg_anim_mandala2 --shapes [frame ...]
//
// --shapes is interlace_check for one unregistered descriptor: the same frame
// rendered as 8-row bands (the reference, then again as the purity control),
// 16-, 4- and 2-row bands, solitary rows==1 calls, and the two parity-skipping
// rows==1 sequences production's interlaced path uses, at 480x480 and again at
// 240x240 (the half-resolution path hands band() half-width rows). A row must
// come out identical in every shape: an animation that derives "which row is
// real" from the call-local offset, or copies a row from a neighbour inside the
// same destination buffer, passes the golden diff and fails here, and would
// paint wrong rows on the device whenever interlacing engages. Exit status is
// non-zero on any mismatch.
//
// Frames are numbered as bench.cpp numbers them (t = 1000 + i*33 ms after one
// warm-up frame at t=0), so frame 120 here is frame 120 of the goldens, and a
// candidate can be laid beside golden/<orig>-120.ppm. Parameters default to
// the descriptor's own; GM_RENDER_P0/P1/P2 override them (0..100), and
// GM_RENDER_THEME selects a theme index (the default theme otherwise, as the
// goldens use). The same W, H, BAND_H and PPM conversion as bench.cpp.
#include "../../src/display/ui/default/bganim/BgAnim.h"
#include "../../src/display/ui/default/bganim/BgAnimCommon.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#ifndef GM_RENDER_CANDIDATE
#error "build with -DGM_RENDER_CANDIDATE=<bg_anim_* descriptor symbol>"
#endif

extern const BgAnimation GM_RENDER_CANDIDATE;

namespace {

constexpr int W = 480;
constexpr int H = 480;
constexpr int BAND_H = 8;
constexpr uint32_t FRAME_MS = 33;

void writePpm(const std::string &path, const uint16_t *fb) {
    FILE *f = fopen(path.c_str(), "wb");
    if (f == nullptr) {
        fprintf(stderr, "cannot write %s\n", path.c_str());
        exit(1);
    }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        const uint16_t c = fb[i];
        uint8_t px[3] = {static_cast<uint8_t>(((c >> 11) & 0x1F) * 255 / 31),
                         static_cast<uint8_t>(((c >> 5) & 0x3F) * 255 / 63),
                         static_cast<uint8_t>((c & 0x1F) * 255 / 31)};
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}

int envInt(const char *name, int def) {
    const char *v = getenv(name);
    return v == nullptr ? def : atoi(v);
}

struct Shape {
    const char *name;
    int bandH;  // rows per call
    int parity; // -1 = contiguous, else rows==1 calls on rows of this parity only
};

const Shape kShapes[] = {
    {"8-row", 8, -1},   {"8-row(ctl)", 8, -1}, {"16-row", 16, -1}, {"4-row", 4, -1}, {"2-row", 2, -1},
    {"rows==1", 1, -1}, {"parity0", 1, 0},     {"parity1", 1, 1},
};
constexpr int kNumShapes = static_cast<int>(sizeof(kShapes) / sizeof(kShapes[0]));

void renderShape(const BgAnimation &anim, const Shape &s, int w, int h, uint32_t t, const uint8_t p[4], uint16_t *fb) {
    if (s.parity < 0) {
        for (int y = 0; y < h; y += s.bandH) {
            const int rows = (y + s.bandH <= h) ? s.bandH : (h - y);
            anim.band(fb + static_cast<size_t>(y) * w, y, rows, w, t, p);
        }
    } else {
        for (int y = s.parity; y < h; y += 2) {
            anim.band(fb + static_cast<size_t>(y) * w, y, 1, w, t, p);
        }
    }
}

long compareShape(const uint16_t *ref, const uint16_t *got, const Shape &s, int w, int h) {
    const int step = (s.parity < 0) ? 1 : 2;
    long bad = 0;
    for (int y = (s.parity < 0) ? 0 : s.parity; y < h; y += step) {
        for (int x = 0; x < w; x++) {
            const size_t i = static_cast<size_t>(y) * w + x;
            bad += ref[i] != got[i];
        }
    }
    return bad;
}

// One init() per size, frame() advanced through every timestep up to the
// requested frame index, then that one state rendered in every shape. Reports
// the worst shape per size and frame; returns the number of failing renders.
int checkShapes(const BgAnimation &anim, const uint8_t p[4], const std::vector<int> &frames, int w, int h) {
    if (anim.release != nullptr) {
        anim.release();
    }
    if (!anim.init(w, h)) {
        printf("%s: init failed at %dx%d\n", anim.id, w, h);
        return 1;
    }
    int last = 0;
    for (int f : frames) {
        last = f > last ? f : last;
    }
    const size_t frameSz = static_cast<size_t>(w) * h;
    std::vector<uint16_t> bufs(frameSz * kNumShapes);
    int failures = 0;
    anim.frame(0, w, h, p);
    for (int i = 0; i <= last; i++) {
        const uint32_t t = 1000 + i * FRAME_MS;
        anim.frame(t, w, h, p);
        bool wanted = false;
        for (int f : frames) {
            wanted = wanted || f == i;
        }
        if (!wanted) {
            continue;
        }
        for (int s = 0; s < kNumShapes; s++) {
            renderShape(anim, kShapes[s], w, h, t, p, bufs.data() + frameSz * s);
        }
        printf("%dx%d frame %03d:", w, h, i);
        bool ok = true;
        for (int s = 1; s < kNumShapes; s++) {
            const long bad = compareShape(bufs.data(), bufs.data() + frameSz * s, kShapes[s], w, h);
            printf(" %s=%ld", kShapes[s].name, bad);
            ok = ok && bad == 0;
        }
        printf("  %s\n", ok ? "OK" : "MISMATCH");
        failures += ok ? 0 : 1;
    }
    if (anim.release != nullptr) {
        anim.release();
    }
    return failures;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s OUTDIR [frame ...]\n", argv[0]);
        return 2;
    }
    const std::string outDir = argv[1];
    const bool shapes = outDir == "--shapes";
    std::vector<int> frames;
    for (int i = 2; i < argc; i++) {
        frames.push_back(atoi(argv[i]));
    }
    if (frames.empty()) {
        frames = {30, 120, 210};
    }
    int last = 0;
    for (int f : frames) {
        last = f > last ? f : last;
    }

    const int theme = envInt("GM_RENDER_THEME", -1);
    if (theme >= 0 && theme < bg_theme_count()) {
        bganim::setThemeStops(bg_theme_stops(theme), 6);
    }

    const BgAnimation &anim = GM_RENDER_CANDIDATE;
    uint8_t p[4] = {anim.params[0].def, anim.params[1].def, anim.params[2].def, anim.params[3].def};
    p[0] = static_cast<uint8_t>(envInt("GM_RENDER_P0", p[0]));
    p[1] = static_cast<uint8_t>(envInt("GM_RENDER_P1", p[1]));
    p[2] = static_cast<uint8_t>(envInt("GM_RENDER_P2", p[2]));

    if (shapes) {
        const int failures = checkShapes(anim, p, frames, W, H) + checkShapes(anim, p, frames, W / 2, H / 2);
        printf("%s shapes: %s\n", anim.id, failures == 0 ? "ALL OK" : "FAILURES");
        return failures == 0 ? 0 : 1;
    }

    std::vector<uint16_t> fb(W * H);
    if (!anim.init(W, H)) {
        fprintf(stderr, "%s: init failed\n", anim.id);
        return 1;
    }
    anim.frame(0, W, H, p);
    anim.band(fb.data(), 0, BAND_H, W, 0, p);
    for (int i = 0; i <= last; i++) {
        const uint32_t t = 1000 + i * FRAME_MS;
        anim.frame(t, W, H, p);
        bool wanted = false;
        for (int f : frames) {
            wanted = wanted || f == i;
        }
        if (!wanted) {
            continue;
        }
        for (int y0 = 0; y0 < H; y0 += BAND_H) {
            anim.band(fb.data() + static_cast<size_t>(y0) * W, y0, BAND_H, W, t, p);
        }
        char name[96];
        snprintf(name, sizeof(name), "/%s-%03d.ppm", anim.id, i);
        writePpm(outDir + name, fb.data());
        printf("%s%s\n", outDir.c_str(), name);
    }
    if (anim.release != nullptr) {
        anim.release();
    }
    return 0;
}
