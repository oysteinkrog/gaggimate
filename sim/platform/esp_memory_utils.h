// Host shim for esp_memory_utils.h.
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// The real function tests a pointer against the running target's PSRAM
// address window. The simulator has no such window (ps_malloc falls back to
// the libc heap, esp_heap_caps.h), so every pointer is equally "not PSRAM";
// false is the truthful answer, matching the honesty of the sim's other
// PSRAM-classification stand-ins (bganim_stub.cpp's zeroed counters).
static inline bool esp_ptr_external_ram(const void *p) {
    (void)p;
    return false;
}

#ifdef __cplusplus
}
#endif
