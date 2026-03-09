#include "pmm.h"
#include "../lib/string.h"

#define PAGE_SIZE 4096
#define MAX_PAGES 32768  // 128 MB max

static uint8_t page_bitmap[MAX_PAGES / 8];
static uint32_t total_pages = 0;
static uint32_t used_pages = 0;

static void set_page(uint32_t page) {
    page_bitmap[page / 8] |= (1 << (page % 8));
}

static void clear_page(uint32_t page) {
    page_bitmap[page / 8] &= ~(1 << (page % 8));
}

static bool test_page(uint32_t page) {
    return (page_bitmap[page / 8] >> (page % 8)) & 1;
}

void pmm_init(uint32_t mem_kb, uint32_t kernel_end) {
    total_pages = (mem_kb * 1024) / PAGE_SIZE;
    if (total_pages > MAX_PAGES) total_pages = MAX_PAGES;

    // Mark all pages as used initially
    memset(page_bitmap, 0xFF, sizeof(page_bitmap));
    used_pages = total_pages;

    // Free pages above kernel end
    uint32_t first_free = (kernel_end + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint32_t i = first_free; i < total_pages; i++) {
        clear_page(i);
        used_pages--;
    }
}

uint32_t pmm_alloc_page() {
    for (uint32_t i = 0; i < total_pages; i++) {
        if (!test_page(i)) {
            set_page(i);
            used_pages++;
            return i * PAGE_SIZE;
        }
    }
    return 0; // Out of memory
}

void pmm_free_page(uint32_t addr) {
    uint32_t page = addr / PAGE_SIZE;
    if (page < total_pages && test_page(page)) {
        clear_page(page);
        used_pages--;
    }
}

uint32_t pmm_get_total_pages() { return total_pages; }
uint32_t pmm_get_used_pages() { return used_pages; }
