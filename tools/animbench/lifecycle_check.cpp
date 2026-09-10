// Animation lifecycle check (gm-bzu.15): init, render, release, again.
//
// The goldens (bench.cpp) and the call-shape check (interlace_check.cpp) each
// init() every animation once and render. Production does something else:
// SleepAnimation switches animations and sizes for as long as the board is
// up, releasing the outgoing one so the next one's hot tables land in the
// 12 KB slab (BgAnimCommon.h). A missed release() keeps slab bytes forever, a
// table sized from a stale width overruns on the next size, and an init()
// that fails halfway must leave the animation retryable. None of that shows
// in a single init. This harness runs the lifecycle and fails on any of it:
//
//   Phase A  Size switches. Every animation through 480, 240, 466, 233 and
//            back, three passes (the first a warm-up), releasing between.
//            init() must succeed,
//            every band must render (two-row bands at even sizes, one-row
//            calls at 233 where the alignment contract in BgAnim.h allows
//            nothing else, and a one-row tail at every size), release() must
//            return the slab to empty and the heap to where it was after
//            the animation's first release.
//   Phase B  Failed allocations. For each animation, the heap is made to run
//            out at each allocation its init() makes in turn (the shim's
//            malloc hook, sticky from that call on). init() must report
//            false, a retry with the fault cleared must succeed and render,
//            and release() must clean up both attempts.
//   Phase C  Slab accounting at the end: nothing left in the slab, no
//            allocHot() ever fell back to PSRAM.
//
// Build with the sanitizers on; without them a stale-width overrun reads a
// neighbouring byte and passes. Makefile.lifecycle does that and runs with
// UBSAN_OPTIONS=halt_on_error=1 so an undefined-behaviour report is a failing
// exit code, not a line in the log. Init failures exit non-zero too; the
// other harnesses print SKIP or INIT FAILED and go on, which is right for a
// bench and wrong for CI.
//
// Build: make -f Makefile.lifecycle check   (in tools/animbench)
#include "../../src/display/ui/default/bganim/BgAnim.h"
#include "../../src/display/ui/default/bganim/BgAnimCommon.h"
#include "shim/esp_heap_caps.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_set>
#include <vector>

using namespace bganim;

namespace {

// ---- fault injection through the shim hooks -------------------------------

// Allocations made through the shim since the counter was reset, the index
// (1-based) from which they fail (0 = none), and the blocks alive right now.
// The fault is sticky: from the k-th call on, every request fails until the
// fault is cleared. bganim::alloc() falls back from ps_malloc to
// heap_caps_malloc, both of which reach this hook, so a single failed call
// would be absorbed by the fall-back and the animation would never see it.
// Sticky is also what memory exhaustion looks like on the device.
unsigned g_allocCalls = 0;
unsigned g_failAt = 0;
unsigned g_faultsFired = 0;
std::unordered_set<void *> g_live;

void *hookMalloc(size_t size) {
    g_allocCalls++;
    if (g_failAt != 0 && g_allocCalls >= g_failAt) {
        g_faultsFired++;
        return nullptr;
    }
    void *p = malloc(size);
    if (p != nullptr) {
        g_live.insert(p);
    }
    return p;
}

void hookFree(void *p) {
    if (p != nullptr) {
        g_live.erase(p);
    }
}

// ---- rendering ------------------------------------------------------------

int rowStride(int w) { return (w + 1) & ~1; }

// Renders the frame the way production's band loop would at this size: two-row
// bands where the alignment contract allows them, one-row calls at an odd
// width, and always one one-row call for the last row so the tail shape is
// exercised at every size. Never calls frame().
void renderAll(const BgAnimation &a, int w, int h, uint32_t t, const uint8_t p[BG_ANIM_PARAMS], std::vector<uint16_t> &fb) {
    const int stride = rowStride(w);
    fb.assign(static_cast<size_t>(stride) * h, 0);
    const int bandRows = (w & 1) ? 1 : 2;
    int y = 0;
    for (; y + bandRows <= h - 1; y += bandRows) {
        a.band(fb.data() + static_cast<size_t>(stride) * y, y, bandRows, w, t, p);
    }
    for (; y < h; y++) {
        a.band(fb.data() + static_cast<size_t>(stride) * y, y, 1, w, t, p);
    }
}

int g_failures = 0;

void fail(const char *anim, const char *fmt, ...) {
    g_failures++;
    printf("FAIL %-10s ", anim);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

void releaseAnim(const BgAnimation &a) {
    if (a.release != nullptr) {
        a.release();
    }
}

} // namespace

int main(int argc, char **argv) {
    const int passes = argc > 1 ? atoi(argv[1]) : 3;
    gm_shim_malloc_hook = hookMalloc;
    gm_shim_free_hook = hookFree;

    const int sizes[] = {480, 240, 466, 233, 480, 233, 466, 240, 480};
    const int nSizes = static_cast<int>(sizeof(sizes) / sizeof(sizes[0]));
    const int n = bg_animation_count();
    std::vector<uint16_t> fb;

    // Heap blocks alive after each animation's release(). Pass 0 is a warm-up
    // with no leak check: the shared tables (the sine LUT in the slab, the
    // 64 KB noise texture from noiseTable()) are built lazily on first use,
    // whichever animation and size gets there first, and live for the
    // process. The baseline is taken at the end of pass 0, when everything
    // lazy has been touched at every size; from then on a block that
    // survives release() is a leak.
    std::vector<long> baselineLive(n, -1);
    unsigned cycles = 0;
    unsigned bands = 0;

    if (passes < 2) {
        printf("need at least 2 passes (pass 0 is the warm-up)\n");
        return 2;
    }
    printf("Phase A: %d passes over %d sizes, %d animations (pass 0 warms up, leak checks from pass 1)\n", passes, nSizes, n);
    for (int pass = 0; pass < passes; pass++) {
        for (int si = 0; si < nSizes; si++) {
            const int size = sizes[si];
            for (int id = 0; id < n; id++) {
                const BgAnimation &a = bg_animation(id);
                uint8_t p[BG_ANIM_PARAMS];
                bg_parse_params(nullptr, id, p);
                const uint32_t failBefore = hotFailCount();
                const size_t slabBefore = hotUsed();
                if (!a.init(size, size)) {
                    fail(a.id, "init(%d) failed (pass %d)", size, pass);
                    releaseAnim(a);
                    continue;
                }
                if (hotFailCount() != failBefore) {
                    fail(a.id, "init(%d) pushed %u hot table(s) out of the slab", size, hotFailCount() - failBefore);
                }
                const uint32_t t = 1000u + 37u * pass + 101u * si;
                a.frame(t, size, size, p);
                renderAll(a, size, size, t, p, fb);
                bands += static_cast<unsigned>(size);
                // A second frame at the same size, so per-frame scratch that
                // depends on the previous frame runs at least once.
                a.frame(t + 33u, size, size, p);
                renderAll(a, size, size, t + 33u, p, fb);
                releaseAnim(a);
                cycles++;
                if (hotUsed() != slabBefore) {
                    fail(a.id, "release() after %d left %u B in the hot slab", size,
                         static_cast<unsigned>(hotUsed() - slabBefore));
                }
                const long live = static_cast<long>(g_live.size());
                if (pass == 0) {
                    baselineLive[id] = live;
                } else if (live != baselineLive[id]) {
                    fail(a.id, "release() after %d left %ld heap block(s) alive (baseline %ld)", size, live, baselineLive[id]);
                    baselineLive[id] = live;
                }
            }
        }
    }
    printf("Phase A: %u init/render/release cycles, %u rows rendered\n\n", cycles, bands);

    printf("Phase B: failed allocations at 480\n");
    unsigned faultCases = 0;
    for (int id = 0; id < n; id++) {
        const BgAnimation &a = bg_animation(id);
        uint8_t p[BG_ANIM_PARAMS];
        bg_parse_params(nullptr, id, p);
        // Count the shim allocations a clean init makes from a released
        // state, then fail from each one onward in turn.
        releaseAnim(a);
        g_allocCalls = 0;
        g_failAt = 0;
        if (!a.init(480, 480)) {
            fail(a.id, "clean init(480) failed before fault injection");
            releaseAnim(a);
            continue;
        }
        const unsigned nAllocs = g_allocCalls;
        releaseAnim(a);
        const long liveBefore = static_cast<long>(g_live.size());
        const size_t slabBefore = hotUsed();
        for (unsigned k = 1; k <= nAllocs; k++) {
            g_allocCalls = 0;
            g_failAt = k;
            const unsigned firedBefore = g_faultsFired;
            const bool ok = a.init(480, 480);
            g_failAt = 0;
            faultCases++;
            if (g_faultsFired == firedBefore) {
                fail(a.id, "fault %u of %u never fired: init made fewer allocations than the clean run", k, nAllocs);
                releaseAnim(a);
                continue;
            }
            if (ok) {
                fail(a.id, "init(480) returned true with allocation %u of %u failed", k, nAllocs);
            }
            // Retry with the fault cleared: init() is documented idempotent.
            if (!a.init(480, 480)) {
                fail(a.id, "retry after failed allocation %u of %u did not recover", k, nAllocs);
                releaseAnim(a);
                continue;
            }
            const uint32_t t = 5000u + 7u * k;
            a.frame(t, 480, 480, p);
            renderAll(a, 480, 480, t, p, fb);
            releaseAnim(a);
            // Against the level before this animation's cases, not zero: a
            // slab leak by an earlier animation pins the slab bottom (the
            // region only resets when every hot table is released) and would
            // otherwise be charged to every animation after it.
            if (hotUsed() != slabBefore) {
                fail(a.id, "release() after failed allocation %u left %u B in the hot slab", k,
                     static_cast<unsigned>(hotUsed() - slabBefore));
            }
            const long live = static_cast<long>(g_live.size());
            if (live != liveBefore) {
                fail(a.id, "failed allocation %u of %u leaked %ld heap block(s)", k, nAllocs, live - liveBefore);
            }
        }
        printf("  %-10s %u allocation(s) in init, failed from each onward\n", a.id, nAllocs);
    }
    printf("Phase B: %u fault cases, %u faults fired\n\n", faultCases, g_faultsFired);

    printf("Phase C: slab accounting\n");
    if (hotUsed() != 0) {
        fail("slab", "%u B still in the hot slab after every release()", static_cast<unsigned>(hotUsed()));
    }
    if (hotFailCount() != 0) {
        fail("slab", "%u allocHot() call(s) fell back to PSRAM", hotFailCount());
    }
    printf("  hot peak %u B, shared %u B, fallbacks %u\n\n", static_cast<unsigned>(hotPeak()), static_cast<unsigned>(hotShared()),
           hotFailCount());

    gm_shim_malloc_hook = nullptr;
    gm_shim_free_hook = nullptr;
    printf("%s (%d failing)\n", g_failures == 0 ? "ALL OK" : "FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
