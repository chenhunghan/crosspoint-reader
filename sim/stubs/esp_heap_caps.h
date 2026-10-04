#pragma once
#include <cstddef>
#include <cstdlib>
#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_8BIT (1 << 2)
#define MALLOC_CAP_DEFAULT (1 << 12)
inline void* heap_caps_malloc(size_t n, unsigned) { return malloc(n); }
inline void* heap_caps_calloc(size_t c, size_t n, unsigned) { return calloc(c, n); }
inline void* heap_caps_realloc(void* p, size_t n, unsigned) { return realloc(p, n); }
inline void heap_caps_free(void* p) { free(p); }
inline size_t heap_caps_get_free_size(unsigned) { return 4 * 1024 * 1024; }
inline size_t heap_caps_get_largest_free_block(unsigned) { return 1024 * 1024; }
inline size_t heap_caps_get_total_size(unsigned) { return 8 * 1024 * 1024; }
inline size_t heap_caps_get_minimum_free_size(unsigned) { return 4 * 1024 * 1024; }
