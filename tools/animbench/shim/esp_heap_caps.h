// Host-build shim for tools/animbench: heap_caps -> plain malloc.
#pragma once
#include <stdlib.h>

#define MALLOC_CAP_INTERNAL 0
#define MALLOC_CAP_8BIT 0
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_DMA 0

// Fault injection for lifecycle_check.cpp. Both hooks default to null, and
// then every allocation is a plain malloc/free as before, so the goldens,
// the call-shape check and the fuzzer see the same behaviour they always did.
// C++17 inline variables: one instance across every TU of the monolithic
// compile, so the harness TU can set them and BgAnimCommon.cpp's alloc()
// sees the change.
inline void *(*gm_shim_malloc_hook)(size_t) = nullptr;
inline void (*gm_shim_free_hook)(void *) = nullptr;

static inline void *gm_shim_malloc(size_t size) { return gm_shim_malloc_hook ? gm_shim_malloc_hook(size) : malloc(size); }
static inline void *heap_caps_malloc(size_t size, int) { return gm_shim_malloc(size); }
static inline void *ps_malloc(size_t size) { return gm_shim_malloc(size); }
static inline void heap_caps_free(void *p) {
    if (gm_shim_free_hook) {
        gm_shim_free_hook(p);
    }
    free(p);
}
// Roomy on purpose: internalHasRoomFor() gates the SRAM-vs-PSRAM placement,
// which is meaningless on the host (both are plain malloc). A big number keeps
// the host taking the same code path as a settled device with headroom.
static inline size_t heap_caps_get_free_size(int) { return 256 * 1024; }
