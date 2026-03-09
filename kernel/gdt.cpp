#include "gdt.h"
#include "../lib/string.h"

struct GDTEntry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} PACKED;

struct GDTPointer {
    uint16_t limit;
    uint32_t base;
} PACKED;

static GDTEntry gdt_entries[5];
static GDTPointer gdt_ptr;

extern "C" void gdt_flush(uint32_t);

static void gdt_set_gate(int idx, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt_entries[idx].base_low    = base & 0xFFFF;
    gdt_entries[idx].base_mid    = (base >> 16) & 0xFF;
    gdt_entries[idx].base_high   = (base >> 24) & 0xFF;
    gdt_entries[idx].limit_low   = limit & 0xFFFF;
    gdt_entries[idx].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt_entries[idx].access      = access;
}

void gdt_init() {
    gdt_ptr.limit = sizeof(gdt_entries) - 1;
    gdt_ptr.base  = (uint32_t)&gdt_entries;

    gdt_set_gate(0, 0, 0, 0, 0);                 // Null segment
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);  // Kernel code
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);  // Kernel data
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);  // User code
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);  // User data

    gdt_flush((uint32_t)&gdt_ptr);
}
