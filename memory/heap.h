#pragma once

#include "../lib/types.h"

void heap_init(uint32_t start, uint32_t size);
void* kmalloc(size_t size);
void* kmalloc_aligned(size_t size, size_t align);
void kfree(void* ptr);
void* krealloc(void* ptr, size_t new_size);
uint32_t heap_get_used();
uint32_t heap_get_total();

// Convenience aliases
static inline uint32_t heap_used() { return heap_get_used(); }
static inline uint32_t heap_free() { return heap_get_total() - heap_get_used(); }
