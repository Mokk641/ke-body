#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_DEFAULT 2
#define MALLOC_CAP_INTERNAL 4
#define MALLOC_CAP_8BIT 8
#define MALLOC_CAP_DMA 16
static inline void *heap_caps_malloc(size_t n, unsigned c) { (void)c; return malloc(n); }
static inline void *heap_caps_calloc(size_t a, size_t b, unsigned c) { (void)c; return calloc(a, b); }
