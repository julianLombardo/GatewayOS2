#pragma once

#include "../lib/types.h"

void pmm_init(uint32_t mem_kb, uint32_t kernel_end);
uint32_t pmm_alloc_page();
void pmm_free_page(uint32_t addr);
uint32_t pmm_get_total_pages();
uint32_t pmm_get_used_pages();

// Convenience aliases
static inline uint32_t pmm_total_page_count() { return pmm_get_total_pages(); }
static inline uint32_t pmm_free_page_count() { return pmm_get_total_pages() - pmm_get_used_pages(); }
