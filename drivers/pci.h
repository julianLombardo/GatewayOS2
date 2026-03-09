#pragma once

#include "../lib/types.h"
#include "ports.h"

// PCI config space access
static inline uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t addr = (1 << 31) | (bus << 16) | (dev << 11) | (func << 8) | (offset & 0xFC);
    outl(0xCF8, addr);
    return inl(0xCFC);
}

static inline void pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t addr = (1 << 31) | (bus << 16) | (dev << 11) | (func << 8) | (offset & 0xFC);
    outl(0xCF8, addr);
    outl(0xCFC, val);
}

static inline uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t val = pci_read32(bus, dev, func, offset);
    return (val >> ((offset & 2) * 8)) & 0xFFFF;
}

struct PciDevice {
    uint8_t bus, dev, func;
    uint16_t vendor_id, device_id;
    uint32_t bar0;
    uint8_t irq;
};

// Find a PCI device by vendor/device ID. Returns true if found.
bool pci_find_device(uint16_t vendor, uint16_t device, PciDevice* out);

// Enable bus mastering for DMA
void pci_enable_bus_master(PciDevice* dev);
