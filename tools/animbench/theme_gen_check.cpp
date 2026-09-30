// Theme publication check (gm-bzu.71): a palette build must read one theme
// from start to end, and a publish must never rewrite the buffer the reader
// holds, however fast the themes change.
//
//   make -f Makefile.theme check
//
// Part 1 runs single-threaded and checks the rules one by one: a build started
// before a publish finishes on the old theme, three publishes in a row leave
// the held buffer alone, and the themeGen() call after such a build does not
// report the new theme as built. Part 2 runs a writer thread that alternates
// two very different themes (six uniform stops against three positional
// ones, dark against bright) as fast as it can while the reader builds
// palettes through themeRGB() and buildThemeRamp(), and requires every
// palette to equal one of the two themes exactly. It exits non-zero on any
// failure.
#include "BgAnimCommon.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {

const uint8_t kA[6][3] = {{0, 0, 0}, {10, 0, 20}, {30, 0, 60}, {60, 10, 90}, {90, 20, 120}, {120, 40, 160}};
const uint8_t kB[3][3] = {{255, 255, 255}, {200, 255, 40}, {255, 120, 0}};
const uint8_t kBPos[3] = {0, 40, 255};

void setA() { bganim::setThemeStops(kA, 6); }
void setB() { bganim::setThemeStopsPos(kB, kBPos, 3); }

struct Pal {
    uint8_t rgb[256][3];
    uint16_t ramp[256];
};

void buildRGB(uint8_t out[256][3]) {
    for (int i = 0; i < 256; i++) {
        bganim::themeRGB(i, out[i]);
    }
}

// A palette build as an animation does it: check, rebuild, record.
void capture(Pal &p) {
    (void)bganim::themeGen();
    buildRGB(p.rgb);
    bganim::buildThemeRamp(p.ramp, 256);
    (void)bganim::themeGen();
}

int g_fail = 0;
void expect(bool ok, const char *what) {
    if (!ok) {
        printf("FAIL: %s\n", what);
        g_fail++;
    }
}

bool sameRGB(const uint8_t a[256][3], const uint8_t b[256][3]) { return memcmp(a, b, 256 * 3) == 0; }

} // namespace

int main() {
    Pal refA, refB;
    setA();
    capture(refA);
    setB();
    capture(refB);
    expect(!sameRGB(refA.rgb, refB.rgb), "the two test themes differ");

    // ---- part 1: the rules, one thread ----------------------------------
    {
        setA();
        const uint32_t gA = bganim::themeGen();
        expect((gA & 0x80000000u) == 0, "a fresh theme reports a real generation");
        uint8_t first[3];
        bganim::themeRGB(0, first); // takes and holds theme A
        setB();
        setA();
        setB(); // three publishes while the reader holds A
        uint8_t got[256][3];
        buildRGB(got);
        expect(sameRGB(got, refA.rgb), "a build that started on A finishes on A after three publishes");
        uint16_t ramp[256];
        bganim::buildThemeRamp(ramp, 256);
        expect(memcmp(ramp, refA.ramp, sizeof ramp) == 0, "buildThemeRamp inside the same build reads A");
        const uint32_t after = bganim::themeGen();
        expect(after != gA, "the call after a raced build does not report the build's generation as current");
        expect((after & 0x80000000u) != 0, "the call after a raced build reports a value no publish produces");
        buildRGB(got);
        expect(sameRGB(got, refB.rgb), "the next build reads the newest theme, B");
        const uint32_t gB = bganim::themeGen();
        expect((gB & 0x80000000u) == 0 && gB != after && gB != gA, "the call after a clean build reports B's generation");
        expect(bganim::themeGen() == gB, "no publish, same generation");

        // A publish between two frames of an animation that reads the theme
        // every frame: the next check must not report the old generation.
        bganim::themeRGB(0, first); // per-frame read, holds B
        setA();
        const uint32_t chk = bganim::themeGen();
        expect(chk != gB, "a publish between per-frame reads is seen at the next check");
        buildRGB(got);
        expect(sameRGB(got, refA.rgb), "and the rebuild reads A");
    }

    // ---- part 2: a writer thread alternating two themes -----------------
    {
        std::atomic<bool> stop{false};
        std::atomic<long> publishes{0};
        std::thread writer([&] {
            bool a = true;
            while (!stop.load(std::memory_order_relaxed)) {
                if (a) {
                    setA();
                } else {
                    setB();
                }
                a = !a;
                publishes.fetch_add(1, std::memory_order_relaxed);
            }
        });
        long builds = 0, seenA = 0, seenB = 0, bad = 0;
        for (int i = 0; i < 200000; i++) {
            Pal p;
            capture(p);
            const bool isA = sameRGB(p.rgb, refA.rgb) && memcmp(p.ramp, refA.ramp, sizeof p.ramp) == 0;
            const bool isB = sameRGB(p.rgb, refB.rgb) && memcmp(p.ramp, refB.ramp, sizeof p.ramp) == 0;
            builds++;
            seenA += isA;
            seenB += isB;
            if (!isA && !isB) {
                if (bad < 5) {
                    printf("  build %ld matches neither theme\n", builds);
                }
                bad++;
            }
        }
        stop.store(true);
        writer.join();
        printf("threaded: %ld builds, %ld publishes, %ld on A, %ld on B, %ld on neither\n", builds, publishes.load(),
               seenA, seenB, bad);
        expect(bad == 0, "every build under concurrent publishes equals one theme");
        expect(seenA > 0 && seenB > 0, "the reader saw both themes (the race was exercised)");

        // After the writer stops, the reader converges on the last theme.
        setB();
        Pal p;
        capture(p);
        expect(sameRGB(p.rgb, refB.rgb), "after the writer stops the reader reads the last theme");
    }

    if (g_fail != 0) {
        printf("theme_gen_check: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("theme_gen_check: PASS\n");
    return 0;
}
