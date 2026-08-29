// LVGL heap wrapper (see gm_lv_mem.cpp). Firmware-side view of the
// accounting; the allocator entry points themselves are declared in
// lv_conf.h so every LVGL translation unit sees them.
#pragma once
#include <stdint.h>

struct GmLvMemStats {
    uint32_t liveBytes;  // bytes currently allocated through LVGL's allocator
    uint32_t hwmBytes;   // high-water mark of liveBytes since boot
    uint32_t liveCount;  // allocations currently outstanding
    uint32_t allocCalls; // cumulative malloc+realloc calls since boot
};

GmLvMemStats gm_lv_mem_stats();
