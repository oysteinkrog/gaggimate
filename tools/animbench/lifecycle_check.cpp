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
//   Phase B  Failed allocations, at 480, 240 and 233. For each animation,
//            the heap is made to run out at each allocation its init()
//            makes in turn (the shim's malloc hook, sticky from that call
//            on). init() must report false, a retry with the fault cleared
//            must succeed and render, and release() must clean up both
//            attempts. Each size runs twice: once with the slab as normal,
//            once with it filled first, so every allocHot() falls back to
//            alloc() and its tables can fail too (gm-bzu.34). An animation
//            that allocates anything must have a fault case in the second.
//   Phase C  Slab accounting at the end: nothing left in the slab, no
//            allocHot() fell back to PSRAM outside the slab-full passes.
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
void renderAll(const BgAnimation &a, int w, int h, uint32_t t, const uint8_t p[4], std::vector<uint16_t> &fb) {
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

// ---- Phase B: failed allocations -------------------------------------------

// allocHot() fallbacks the slab-full passes caused on purpose, so Phase C can
// tell them from a table that genuinely did not fit.
uint32_t g_forcedFallbacks = 0;

// Takes every free byte of the slab's bottom end, so each allocHot() an
// animation makes falls back to alloc() and reaches the malloc hook. Without
// this a table that fits the slab can never fail on the host: the slab is a
// static array, and an animation whose tables are all hot had no fault cases
// at all. On the device the fallback is what runs when the slab is full, so
// that is the path a fault has to reach. Returns the filler's size, 0 if the
// slab was already full.
size_t fillSlab(void *&filler) {
    filler = nullptr;
    const size_t top = HOT_SLAB_BYTES - hotShared();
    const size_t cap = HOT_SLAB_BYTES - HOT_SHARED_RESERVE;
    const size_t limit = top < cap ? top : cap;
    if (hotUsed() >= limit) {
        return 0;
    }
    const size_t n = limit - hotUsed();
    const uint32_t failBefore = hotFailCount();
    filler = allocHot(n);
    if (hotFailCount() != failBefore) {
        g_forcedFallbacks += hotFailCount() - failBefore;
        fail("slab", "filler of %u B did not fit the free slab", static_cast<unsigned>(n));
    }
    return n;
}

// For each animation, makes the heap run out at each allocation its init()
// makes in turn (the shim's malloc hook, sticky from that call on). init()
// must report false, a retry with the fault cleared must succeed and render,
// and release() must clean up both attempts. With slabFull, the slab is
// filled first so the hot tables are among the allocations that fail, and an
// animation that allocates anything must have at least one fault case.
unsigned faultPhase(int size, bool slabFull, std::vector<uint16_t> &fb) {
    printf("  at %d, slab %s\n", size, slabFull ? "full" : "as normal");
    unsigned faultCases = 0;
    for (int id = 0; id < bg_animation_count(); id++) {
        const BgAnimation &a = bg_animation(id);
        uint8_t p[4];
        bg_parse_params(nullptr, id, p);
        releaseAnim(a);

        // What a clean init takes with the slab as normal: hook calls and
        // slab bytes. Either one means the animation allocates.
        g_allocCalls = 0;
        g_failAt = 0;
        const size_t slabIdle = hotUsed();
        if (!a.init(size, size)) {
            fail(a.id, "clean init(%d) failed before fault injection", size);
            releaseAnim(a);
            continue;
        }
        const bool allocates = g_allocCalls != 0 || hotUsed() != slabIdle;
        releaseAnim(a);

        void *filler = nullptr;
        size_t fillerBytes = 0;
        if (slabFull) {
            fillerBytes = fillSlab(filler);
        }
        const uint32_t failBase = hotFailCount();

        // Count the shim allocations a clean init makes in this slab state,
        // then fail from each one onward in turn.
        g_allocCalls = 0;
        if (!a.init(size, size)) {
            fail(a.id, "clean init(%d) failed with the slab %s", size, slabFull ? "full" : "as normal");
            releaseAnim(a);
            if (filler != nullptr) {
                release(filler, fillerBytes);
            }
            g_forcedFallbacks += hotFailCount() - failBase;
            continue;
        }
        const unsigned nAllocs = g_allocCalls;
        releaseAnim(a);
        if (slabFull && allocates && nAllocs == 0) {
            fail(a.id, "allocates at %d but no allocation reached the fault hook with the slab full", size);
        }
        const long liveBefore = static_cast<long>(g_live.size());
        const size_t slabBefore = hotUsed();
        for (unsigned k = 1; k <= nAllocs; k++) {
            g_allocCalls = 0;
            g_failAt = k;
            const unsigned firedBefore = g_faultsFired;
            const bool ok = a.init(size, size);
            g_failAt = 0;
            faultCases++;
            if (g_faultsFired == firedBefore) {
                fail(a.id, "fault %u of %u at %d never fired: init made fewer allocations than the clean run", k, nAllocs, size);
                releaseAnim(a);
                continue;
            }
            if (ok) {
                fail(a.id, "init(%d) returned true with allocation %u of %u failed", size, k, nAllocs);
            }
            // Retry with the fault cleared: init() is documented idempotent.
            if (!a.init(size, size)) {
                fail(a.id, "retry at %d after failed allocation %u of %u did not recover", size, k, nAllocs);
                releaseAnim(a);
                continue;
            }
            const uint32_t t = 5000u + 7u * k;
            a.frame(t, size, size, p);
            renderAll(a, size, size, t, p, fb);
            releaseAnim(a);
            // Against the level before this animation's cases, not zero: a
            // slab leak by an earlier animation pins the slab bottom (the
            // region only resets when every hot table is released) and would
            // otherwise be charged to every animation after it.
            if (hotUsed() != slabBefore) {
                fail(a.id, "release() at %d after failed allocation %u left %u B in the hot slab", size, k,
                     static_cast<unsigned>(hotUsed() - slabBefore));
            }
            const long live = static_cast<long>(g_live.size());
            if (live != liveBefore) {
                fail(a.id, "failed allocation %u of %u at %d leaked %ld heap block(s)", k, nAllocs, size, live - liveBefore);
            }
        }
        if (filler != nullptr) {
            release(filler, fillerBytes);
        }
        if (slabFull) {
            g_forcedFallbacks += hotFailCount() - failBase;
        }
        printf("    %-10s %u allocation(s) in init, failed from each onward\n", a.id, nAllocs);
    }
    return faultCases;
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
                uint8_t p[4];
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

    printf("Phase B: failed allocations at 480, 240 and 233, slab as normal and slab full\n");
    unsigned faultCases = 0;
    const int faultSizes[] = {480, 240, 233};
    for (const int size : faultSizes) {
        for (const bool slabFull : {false, true}) {
            faultCases += faultPhase(size, slabFull, fb);
        }
    }
    printf("Phase B: %u fault cases, %u faults fired\n\n", faultCases, g_faultsFired);

    printf("Phase C: slab accounting\n");
    if (hotUsed() != 0) {
        fail("slab", "%u B still in the hot slab after every release()", static_cast<unsigned>(hotUsed()));
    }
    // The slab-full passes in Phase B push tables out on purpose; any other
    // fallback means a table did not fit the slab it was sized for.
    if (hotFailCount() != g_forcedFallbacks) {
        fail("slab", "%u allocHot() call(s) fell back to PSRAM outside the slab-full passes", hotFailCount() - g_forcedFallbacks);
    }
    printf("  hot peak %u B, shared %u B, fallbacks %u (%u forced by the slab-full passes)\n\n", static_cast<unsigned>(hotPeak()),
           static_cast<unsigned>(hotShared()), hotFailCount(), g_forcedFallbacks);

    gm_shim_malloc_hook = nullptr;
    gm_shim_free_hook = nullptr;
    printf("%s (%d failing)\n", g_failures == 0 ? "ALL OK" : "FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
