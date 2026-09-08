// Host shim for esp_heap_caps.h: heap-cap allocs map onto the libc heap.
#pragma once

#include <malloc.h> // malloc_usable_size, for heap_caps_get_allocated_size
#include <stdint.h>
#include <stdlib.h>

#define MALLOC_CAP_8BIT 0
#define MALLOC_CAP_32BIT 0
#define MALLOC_CAP_DMA 0
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_INTERNAL 0
#define MALLOC_CAP_DEFAULT 0

static inline void *heap_caps_malloc(size_t size, uint32_t caps) {
    (void)caps;
    return malloc(size);
}
static inline void *heap_caps_calloc(size_t n, size_t size, uint32_t caps) {
    (void)caps;
    return calloc(n, size);
}
static inline void *heap_caps_realloc(void *ptr, size_t size, uint32_t caps) {
    (void)caps;
    return realloc(ptr, size);
}
static inline void heap_caps_free(void *ptr) { free(ptr); }
// glibc's real answer to "how many bytes does this block actually occupy",
// same question heap_caps_get_allocated_size answers on the device (block
// size, not requested size). malloc_usable_size is its host equivalent.
static inline size_t heap_caps_get_allocated_size(void *ptr) { return malloc_usable_size(ptr); }
// No PSRAM/threshold split on the host; the libc heap is the only heap.
static inline void heap_caps_malloc_extmem_enable(size_t limit) { (void)limit; }
// Report a generous, fixed budget so memory-watching UI code stays happy.
static inline size_t heap_caps_get_free_size(uint32_t caps) {
    (void)caps;
    return 4u * 1024u * 1024u;
}
static inline size_t heap_caps_get_total_size(uint32_t caps) {
    (void)caps;
    return 8u * 1024u * 1024u;
}
static inline size_t heap_caps_get_largest_free_block(uint32_t caps) {
    (void)caps;
    return 2u * 1024u * 1024u;
}
// Low-water mark since boot. Nothing tracks it on the host, so report the same
// fixed figure as the current free size: "never dipped".
static inline size_t heap_caps_get_minimum_free_size(uint32_t caps) {
    (void)caps;
    return 4u * 1024u * 1024u;
}

// Field names and shape match the real multi_heap_info_t; block counts have
// no host equivalent (the libc heap does not expose them through this API)
// and are reported as 0 rather than guessed at.
typedef struct multi_heap_info {
    size_t total_free_bytes;
    size_t total_allocated_bytes;
    size_t largest_free_block;
    size_t minimum_free_bytes;
    size_t allocated_blocks;
    size_t free_blocks;
    size_t total_blocks;
} multi_heap_info_t;

static inline void heap_caps_get_info(multi_heap_info_t *info, uint32_t caps) {
    info->total_free_bytes = heap_caps_get_free_size(caps);
    info->total_allocated_bytes = heap_caps_get_total_size(caps) - info->total_free_bytes;
    info->largest_free_block = heap_caps_get_largest_free_block(caps);
    info->minimum_free_bytes = heap_caps_get_minimum_free_size(caps);
    info->allocated_blocks = 0;
    info->free_blocks = 0;
    info->total_blocks = 0;
}
