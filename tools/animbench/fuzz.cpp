// animfuzz — parameter/time-space sweep of the bganim fleet under ASan/UBSan.
//
// The bench harness only ever renders each animation at its default parameters
// into one full-size framebuffer, so it cannot see two whole classes of bug:
//
//   1. A band() that indexes dst by absolute y instead of (y - y0). With a
//      full framebuffer behind the pointer that write lands somewhere valid.
//      Here each band gets its own exactly-sized heap block, so ASan traps it.
//   2. A lookup table whose padding covers the default parameters but not the
//      extremes. Every parameter is 0-100 and every combination is reachable
//      from the web UI, so "works at the defaults" proves nothing.
//
// Time is swept too: tMs is millis(), which reaches 4.2e9 before it wraps, and
// fixed-point time math that is fine at t=1e3 can overflow at t=1e9.
//
//   make -f Makefile.fuzz && ./build/fuzz            all animations
//   ./build/fuzz --anim 4                            one registry id
#include "../../src/display/ui/default/bganim/BgAnim.h"
#include "../../src/display/ui/default/bganim/BgAnimCommon.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

namespace {

constexpr int W = 480;
constexpr int H = 480;
constexpr int BAND_H = 16;

// millis() values worth probing: startup, a few minutes, a few hours, ~11
// days, and one step short of the uint32 wrap.
const uint32_t TIMES[] = {0u, 33u, 100000u, 5000000u, 1000000000u, 4294900000u};

struct ParamSet {
    const char *why;
    uint8_t p[4];
};

// Builds the parameter sets probed for one animation: the defaults, the two
// all-extreme corners, every single parameter pushed to each end on its own
// (so a failure names one parameter), and pseudorandom combinations for the
// interactions those miss.
std::vector<ParamSet> paramSets(const BgAnimation &anim, int nRandom) {
    std::vector<ParamSet> out;
    uint8_t def[4] = {anim.params[0].def, anim.params[1].def, anim.params[2].def, anim.params[3].def};
    for (int i = 0; i < 4; i++) {
        if (anim.params[i].key == nullptr) {
            def[i] = 0; // bg_parse_params zeroes unused slots
        }
    }
    out.push_back({"defaults", {def[0], def[1], def[2], def[3]}});
    out.push_back({"all-min", {0, 0, 0, 0}});
    out.push_back({"all-max", {100, 100, 100, 100}});

    static char labels[8][32];
    int nLabel = 0;
    for (int i = 0; i < 4; i++) {
        if (anim.params[i].key == nullptr) {
            continue;
        }
        for (uint8_t v : {static_cast<uint8_t>(0), static_cast<uint8_t>(100)}) {
            ParamSet s{nullptr, {def[0], def[1], def[2], def[3]}};
            s.p[i] = v;
            snprintf(labels[nLabel], sizeof(labels[0]), "%s=%u", anim.params[i].key, v);
            s.why = labels[nLabel++];
            out.push_back(s);
        }
    }

    static char rlabels[256][32];
    uint32_t seed = 0x1234567u;
    for (int r = 0; r < nRandom && r < 256; r++) {
        ParamSet s{nullptr, {0, 0, 0, 0}};
        for (int i = 0; i < 4; i++) {
            s.p[i] = anim.params[i].key ? static_cast<uint8_t>(bganim::nextRand(seed) % 101) : 0;
        }
        snprintf(rlabels[r], sizeof(rlabels[0]), "rand(%u,%u,%u,%u)", s.p[0], s.p[1], s.p[2], s.p[3]);
        s.why = rlabels[r];
        out.push_back(s);
    }
    return out;
}

// Theme shapes an animation's palette build, and the stop count is variable
// (2-8), so a ramp walker sized for the 6-stop built-ins is worth probing at
// both ends.
void applyTheme(int which) {
    if (which < bg_theme_count()) {
        bganim::setThemeStops(bg_theme_stops(which), 6);
        return;
    }
    static const uint8_t two[2][3] = {{0, 0, 0}, {255, 255, 255}};
    static const uint8_t eight[8][3] = {{1, 1, 2},     {20, 8, 40},    {60, 10, 90},  {120, 20, 80},
                                        {200, 60, 40}, {240, 140, 30}, {250, 220, 90}, {255, 255, 255}};
    if (which == bg_theme_count()) {
        bganim::setThemeStops(two, 2);
    } else {
        bganim::setThemeStops(eight, 8);
    }
}

} // namespace

int main(int argc, char **argv) {
    int onlyAnim = -1;
    int nRandom = 24;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--anim") == 0 && i + 1 < argc) {
            onlyAnim = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--random") == 0 && i + 1 < argc) {
            nRandom = atoi(argv[++i]);
        } else {
            fprintf(stderr, "unknown arg %s\n", argv[i]);
            return 1;
        }
    }

    const int nThemes = bg_theme_count() + 2; // built-ins + 2-stop and 8-stop custom
    unsigned long long bands = 0;

    for (int id = 0; id < bg_animation_count(); id++) {
        if (onlyAnim >= 0 && id != onlyAnim) {
            continue;
        }
        const BgAnimation &anim = bg_animation(id);
        if (!anim.init(W, H)) {
            printf("%-11s INIT FAILED\n", anim.id);
            continue;
        }
        const std::vector<ParamSet> sets = paramSets(anim, nRandom);
        int themeIdx = 0;
        for (const ParamSet &s : sets) {
            // Rotate the theme across parameter sets rather than nesting the
            // two sweeps: themes are cheap to get wrong in the same place for
            // every animation, and the full cross product is 30x the runtime.
            applyTheme(themeIdx++ % nThemes);
            // init() is documented idempotent and is where param-dependent
            // LUTs get rebuilt, so call it again on every set.
            if (!anim.init(W, H)) {
                printf("%-11s INIT FAILED (%s)\n", anim.id, s.why);
                continue;
            }
            for (uint32_t t : TIMES) {
                anim.frame(t, W, H, s.p);
                for (int y0 = 0; y0 < H; y0 += BAND_H) {
                    // Exactly-sized block: any write outside rows [y0,y0+16)
                    // or columns [0,w) is a heap overflow ASan will catch.
                    std::vector<uint16_t> dst(static_cast<size_t>(W) * BAND_H, 0);
                    anim.band(dst.data(), y0, BAND_H, W, t, s.p);
                    bands++;
                }
            }
        }
        printf("%-11s ok  (%zu param sets x %zu times x %d bands)\n", anim.id, sets.size(),
               sizeof(TIMES) / sizeof(TIMES[0]), H / BAND_H);
        fflush(stdout);
    }
    printf("\n%llu bands rendered clean\n", bands);
    return 0;
}
