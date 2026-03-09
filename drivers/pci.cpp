#include "pci.h"

bool pci_find_device(uint16_t vendor, uint16_t device, PciDevice* out) {
    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            uint32_t reg0 = pci_read32(bus, dev, 0, 0);
            uint16_t vid = reg0 & 0xFFFF;
            uint16_t did = (reg0 >> 16) & 0xFFFF;
            if (vid == 0xFFFF) continue;
            if (vid == vendor && did == device) {
                out->bus = bus;
                out->dev = dev;
                out->func = 0;
                out->vendor_id = vid;
                out->device_id = did;
                out->bar0 = pci_read32(bus, dev, 0, 0x10) & 0xFFFFFFF0;
                out->irq = pci_read32(bus, dev, 0, 0x3C) & 0xFF;
                return true;
            }
        }
    }
    return false;
}

void pci_enable_bus_master(PciDevice* dev) {
    uint32_t cmd = pci_read32(dev->bus, dev->dev, dev->func, 0x04);
    cmd |= (1 << 2); // Bus Master Enable
    pci_write32(dev->bus, dev->dev, dev->func, 0x04, cmd);
}
