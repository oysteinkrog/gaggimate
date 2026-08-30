// Host-build shim for tools/animbench: heap_caps -> plain malloc.
#pragma once
#include <stdlib.h>

#define MALLOC_CAP_INTERNAL 0
#define MALLOC_CAP_8BIT 0
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_DMA 0

static inline void *heap_caps_malloc(size_t size, int) { return malloc(size); }
static inline void *ps_malloc(size_t size) { return malloc(size); }
static inline void heap_caps_free(void *p) { free(p); }
// Roomy on purpose: internalHasRoomFor() gates the SRAM-vs-PSRAM placement,
// which is meaningless on the host (both are plain malloc). A big number keeps
// the host taking the same code path as a settled device with headroom.
static inline size_t heap_caps_get_free_size(int) { return 256 * 1024; }
