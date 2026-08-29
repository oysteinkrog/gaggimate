// LVGL's allocator, wrapped. Behavior is identical to the ps_malloc/free/
// ps_realloc macros lv_conf.h used to name directly; the wrapper only adds
// live-set accounting. Why: LVGL's entire heap (objects, styles, draw
// descriptors) lives in PSRAM, which makes every lv_obj_redraw tree walk a
// random PSRAM pointer chase -- the dominant cost of the snapshot's draw
// stage on device. Whether that heap could instead fit an internal-RAM
// arena is a sizing question, and these counters answer it from the rig
// (GM_LVMEM log line, loadtest builds). This is also the single hook a
// placement policy would go in if the sizing works out.
#include "gm_lv_mem.h"
#include <atomic>
#include <esp32-hal-psram.h>
#include <esp_heap_caps.h>

namespace {
// Relaxed atomics: LVGL allocates almost exclusively on the UI task, but
// nothing enforces that, and a torn counter would misreport the sizing this
// exists to measure. Accounting uses the allocator's real block size
// (heap_caps_get_allocated_size), not the requested size, so the numbers
// include per-block overhead and match what an arena would actually need.
std::atomic<uint32_t> s_liveBytes{0};
std::atomic<uint32_t> s_hwmBytes{0};
std::atomic<uint32_t> s_liveCount{0};
std::atomic<uint32_t> s_allocCalls{0};

void addLive(uint32_t sz) {
    uint32_t live = s_liveBytes.fetch_add(sz, std::memory_order_relaxed) + sz;
    uint32_t hwm = s_hwmBytes.load(std::memory_order_relaxed);
    while (live > hwm && !s_hwmBytes.compare_exchange_weak(hwm, live, std::memory_order_relaxed)) {
    }
}
} // namespace

extern "C" void *gm_lv_malloc(size_t size) {
    void *p = ps_malloc(size);
    if (p != nullptr) {
        s_allocCalls.fetch_add(1, std::memory_order_relaxed);
        s_liveCount.fetch_add(1, std::memory_order_relaxed);
        addLive(heap_caps_get_allocated_size(p));
    }
    return p;
}

extern "C" void gm_lv_free(void *ptr) {
    if (ptr == nullptr)
        return;
    s_liveBytes.fetch_sub(heap_caps_get_allocated_size(ptr), std::memory_order_relaxed);
    s_liveCount.fetch_sub(1, std::memory_order_relaxed);
    free(ptr);
}

extern "C" void *gm_lv_realloc(void *ptr, size_t size) {
    uint32_t oldSz = ptr != nullptr ? heap_caps_get_allocated_size(ptr) : 0;
    void *p = ps_realloc(ptr, size);
    if (p == nullptr)
        return nullptr; // old block still valid, counters unchanged
    s_allocCalls.fetch_add(1, std::memory_order_relaxed);
    if (ptr == nullptr)
        s_liveCount.fetch_add(1, std::memory_order_relaxed);
    s_liveBytes.fetch_sub(oldSz, std::memory_order_relaxed);
    addLive(heap_caps_get_allocated_size(p));
    return p;
}

GmLvMemStats gm_lv_mem_stats() {
    GmLvMemStats s;
    s.liveBytes = s_liveBytes.load(std::memory_order_relaxed);
    s.hwmBytes = s_hwmBytes.load(std::memory_order_relaxed);
    s.liveCount = s_liveCount.load(std::memory_order_relaxed);
    s.allocCalls = s_allocCalls.load(std::memory_order_relaxed);
    return s;
}
