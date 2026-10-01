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
// A second pass checks destination alignment (gm-bzu.49). BgAnim.h promises
// band() only a 4-byte-aligned dst, and ee.vst.128.ip silently clears the low
// four address bits, so a PIE kernel that trusts the pointer writes before its
// span. The host never compiles the asm, but AnimOrbits, AnimFireflies,
// AnimRipples and AnimSteam (gm-1wrm) run their fill glue here through host
// twins that store the way the vector store does, masked to 16 bytes. So every animation is rendered at 480, 240, 466 and
// 233 px into a buffer 0, 4, 8 and 12 bytes past a 16-byte boundary, with
// sentinel guard zones on both sides, and each result must match bandRef()
// rendered into an aligned buffer, with both guards untouched. Rows per call
// are 1 and 2 (the device's BAND_H) for an even width and 1 for 233, the
// shapes BgAnim.h allows; the 2-row call at 466 is the one whose second row
// starts 4 mod 16 even from an aligned dst.
//
//   make -f Makefile.fuzz && ./build/fuzz            all animations, both passes
//   ./build/fuzz --anim 4                            one registry id
//   ./build/fuzz --align-only                        only the alignment pass
#include "../../src/display/ui/default/bganim/BgAnim.h"
#include "../../src/display/ui/default/bganim/BgAnimCommon.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
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
    uint8_t p[BG_ANIM_PARAMS];
};

// Builds the parameter sets probed for one animation: the defaults, the two
// all-extreme corners, every single parameter pushed to each end on its own
// (so a failure names one parameter), and pseudorandom combinations for the
// interactions those miss.
std::vector<ParamSet> paramSets(const BgAnimation &anim, int nRandom) {
    std::vector<ParamSet> out;
    ParamSet defs{"defaults", {}};
    for (int i = 0; i < BG_ANIM_PARAMS; i++) {
        defs.p[i] = anim.params[i].key != nullptr ? anim.params[i].def : 0; // bg_parse_params zeroes unused slots
    }
    out.push_back(defs);
    ParamSet lo{"all-min", {}};
    ParamSet hi{"all-max", {}};
    for (int i = 0; i < BG_ANIM_PARAMS; i++) {
        hi.p[i] = 100;
    }
    out.push_back(lo);
    out.push_back(hi);

    static char labels[2 * BG_ANIM_PARAMS][32];
    int nLabel = 0;
    for (int i = 0; i < BG_ANIM_PARAMS; i++) {
        if (anim.params[i].key == nullptr) {
            continue;
        }
        for (uint8_t v : {static_cast<uint8_t>(0), static_cast<uint8_t>(100)}) {
            ParamSet s = defs;
            s.p[i] = v;
            snprintf(labels[nLabel], sizeof(labels[0]), "%s=%u", anim.params[i].key, v);
            s.why = labels[nLabel++];
            out.push_back(s);
        }
    }

    static char rlabels[256][48];
    uint32_t seed = 0x1234567u;
    for (int r = 0; r < nRandom && r < 256; r++) {
        ParamSet s{nullptr, {}};
        for (int i = 0; i < BG_ANIM_PARAMS; i++) {
            s.p[i] = anim.params[i].key ? static_cast<uint8_t>(bganim::nextRand(seed) % 101) : 0;
        }
        snprintf(rlabels[r], sizeof(rlabels[0]), "rand(%u,%u,%u,%u,%u,%u,%u,%u)", s.p[0], s.p[1], s.p[2], s.p[3], s.p[4], s.p[5],
                 s.p[6], s.p[7]);
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

// Pixels of sentinel on each side of the destination. A masked 16-byte store
// reaches at most 12 bytes before the span; 16 pixels is 32 bytes.
constexpr int GUARD_PX = 16;
constexpr uint16_t SENTINEL = 0xA5C3;
const int ALIGN_WIDTHS[] = {480, 240, 466, 233};
const int ALIGN_OFFSETS[] = {0, 4, 8, 12}; // bytes past a 16-byte boundary

// Renders every band of one frame at width w through band() at each dst
// offset and compares with bandRef(). Returns the number of failures, and
// prints the first few.
int alignCheck(const BgAnimation &anim, int w, const uint8_t p[BG_ANIM_PARAMS], uint32_t t, const char *why) {
    const int h = w;
    anim.frame(t, w, h, p);
    int failures = 0;
    const int maxRows = (w & 1) ? 1 : 2;
    for (int rows = 1; rows <= maxRows; rows++) {
        const size_t n = static_cast<size_t>(w) * rows;
        // One block per shape: guard, up to 12 bytes of offset, the span,
        // guard. uint16_t storage, base rounded up to 16 bytes by hand.
        std::vector<uint16_t> ref(n + 8);
        uint16_t *refDst = reinterpret_cast<uint16_t *>((reinterpret_cast<uintptr_t>(ref.data()) + 15) & ~uintptr_t{15});
        std::vector<uint16_t> buf(n + 2 * GUARD_PX + 16);
        uint16_t *base = reinterpret_cast<uint16_t *>(
            (reinterpret_cast<uintptr_t>(buf.data() + GUARD_PX) + 15) & ~uintptr_t{15});
        for (int y0 = 0; y0 + rows <= h; y0 += rows) {
            anim.bandRef(refDst, y0, rows, w, t, p);
            for (int off : ALIGN_OFFSETS) {
                std::fill(buf.begin(), buf.end(), SENTINEL);
                uint16_t *dst = base + off / 2;
                anim.band(dst, y0, rows, w, t, p);
                int badBefore = 0, badAfter = 0, badPx = 0, firstPx = -1;
                for (uint16_t *q = buf.data(); q < dst; q++) {
                    badBefore += *q != SENTINEL;
                }
                for (uint16_t *q = dst + n; q < buf.data() + buf.size(); q++) {
                    badAfter += *q != SENTINEL;
                }
                for (size_t i = 0; i < n; i++) {
                    if (dst[i] != refDst[i]) {
                        if (firstPx < 0) {
                            firstPx = static_cast<int>(i);
                        }
                        badPx++;
                    }
                }
                if (badBefore || badAfter || badPx) {
                    if (failures < 4) {
                        printf("%-11s ALIGN FAIL w=%d rows=%d y0=%d off=%d (%s, t=%u): %d px differ from bandRef "
                               "(first row %d col %d), guard before %d, after %d\n",
                               anim.id, w, rows, y0, off, why, t, badPx, firstPx < 0 ? -1 : firstPx / w,
                               firstPx < 0 ? -1 : firstPx % w, badBefore, badAfter);
                    }
                    failures++;
                }
            }
        }
    }
    return failures;
}

} // namespace

int main(int argc, char **argv) {
    int onlyAnim = -1;
    int nRandom = 24;
    bool alignOnly = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--anim") == 0 && i + 1 < argc) {
            onlyAnim = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--random") == 0 && i + 1 < argc) {
            nRandom = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--align-only") == 0) {
            alignOnly = true;
        } else {
            fprintf(stderr, "unknown arg %s\n", argv[i]);
            return 1;
        }
    }

    const int nThemes = bg_theme_count() + 2; // built-ins + 2-stop and 8-stop custom
    unsigned long long bands = 0;

    for (int id = 0; id < bg_animation_count() && !alignOnly; id++) {
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
    if (!alignOnly) {
        printf("\n%llu bands rendered clean\n", bands);
    }

    // Alignment pass: defaults and both corners, two times, every width and
    // offset. One theme; the palette is not what this pass is about.
    applyTheme(0);
    int alignFailures = 0;
    for (int id = 0; id < bg_animation_count(); id++) {
        if (onlyAnim >= 0 && id != onlyAnim) {
            continue;
        }
        const BgAnimation &anim = bg_animation(id);
        const std::vector<ParamSet> sets = paramSets(anim, 0); // defaults, corners, single-parameter ends
        int animFailures = 0;
        for (int w : ALIGN_WIDTHS) {
            for (size_t si = 0; si < sets.size() && si < 3; si++) {
                anim.release(); // a new size: start from nothing, as the device does
                if (!anim.init(w, w)) {
                    printf("%-11s INIT FAILED at %d (%s)\n", anim.id, w, sets[si].why);
                    animFailures++;
                    continue;
                }
                for (uint32_t t : {33u, 5000000u}) {
                    animFailures += alignCheck(anim, w, sets[si].p, t, sets[si].why);
                }
            }
        }
        anim.release();
        printf("%-11s align %s (widths 480 240 466 233, dst +0 +4 +8 +12 bytes)\n", anim.id, animFailures ? "FAIL" : "ok");
        fflush(stdout);
        alignFailures += animFailures;
    }
    if (alignFailures) {
        printf("\n%d alignment failures\n", alignFailures);
        return 1;
    }
    printf("\nalignment pass clean\n");
    return 0;
}
