// Host-build shim for tools/animbench: heap_caps -> plain malloc.
#pragma once
#include <stdlib.h>

#define MALLOC_CAP_INTERNAL 0
#define MALLOC_CAP_8BIT 0

static inline void *heap_caps_malloc(size_t size, int) { return malloc(size); }
static inline void *ps_malloc(size_t size) { return malloc(size); }
