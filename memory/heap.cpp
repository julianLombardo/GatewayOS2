#include "heap.h"
#include "../lib/string.h"

struct HeapBlock {
    uint32_t size;      // Size of this block (including header)
    bool     free;
    HeapBlock* next;
};

static HeapBlock* heap_start = NULL;
static uint32_t heap_total = 0;

void heap_init(uint32_t start, uint32_t size) {
    heap_start = (HeapBlock*)start;
    heap_start->size = size;
    heap_start->free = true;
    heap_start->next = NULL;
    heap_total = size;
}

void* kmalloc(size_t size) {
    size = (size + 15) & ~15; // 16-byte align
    uint32_t total_size = size + sizeof(HeapBlock);

    HeapBlock* block = heap_start;
    while (block) {
        if (block->free && block->size >= total_size) {
            // Split if enough room
            if (block->size > total_size + sizeof(HeapBlock) + 16) {
                HeapBlock* new_block = (HeapBlock*)((uint8_t*)block + total_size);
                new_block->size = block->size - total_size;
                new_block->free = true;
                new_block->next = block->next;
                block->size = total_size;
                block->next = new_block;
            }
            block->free = false;
            return (void*)((uint8_t*)block + sizeof(HeapBlock));
        }
        block = block->next;
    }
    return NULL; // Out of memory
}

void* kmalloc_aligned(size_t size, size_t align) {
    // Simple: allocate extra and align within
    void* p = kmalloc(size + align);
    if (!p) return NULL;
    uintptr_t addr = ((uintptr_t)p + align - 1) & ~(align - 1);
    return (void*)addr;
}

void kfree(void* ptr) {
    if (!ptr) return;
    HeapBlock* block = (HeapBlock*)((uint8_t*)ptr - sizeof(HeapBlock));
    block->free = true;

    // Coalesce adjacent free blocks
    HeapBlock* b = heap_start;
    while (b) {
        if (b->free && b->next && b->next->free) {
            b->size += b->next->size;
            b->next = b->next->next;
            continue; // Check again
        }
        b = b->next;
    }
}

void* krealloc(void* ptr, size_t new_size) {
    if (!ptr) return kmalloc(new_size);
    HeapBlock* block = (HeapBlock*)((uint8_t*)ptr - sizeof(HeapBlock));
    uint32_t old_size = block->size - sizeof(HeapBlock);
    if (new_size <= old_size) return ptr;
    void* new_ptr = kmalloc(new_size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, old_size);
        kfree(ptr);
    }
    return new_ptr;
}

uint32_t heap_get_used() {
    uint32_t used = 0;
    HeapBlock* b = heap_start;
    while (b) {
        if (!b->free) used += b->size;
        b = b->next;
    }
    return used;
}

uint32_t heap_get_total() { return heap_total; }
