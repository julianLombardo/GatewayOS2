#include "ata.h"
#include "ports.h"
#include "serial.h"
#include "../lib/printf.h"

// Primary IDE channel I/O ports
#define ATA_DATA        0x1F0
#define ATA_ERROR       0x1F1
#define ATA_SECT_COUNT  0x1F2
#define ATA_LBA_LO     0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HI     0x1F5
#define ATA_DRIVE_SEL   0x1F6
#define ATA_STATUS      0x1F7
#define ATA_COMMAND     0x1F7

// Status bits
#define ATA_SR_BSY      0x80
#define ATA_SR_DRDY     0x40
#define ATA_SR_DRQ      0x08
#define ATA_SR_ERR      0x01

// Commands
#define ATA_CMD_READ    0x20
#define ATA_CMD_WRITE   0x30
#define ATA_CMD_FLUSH   0xE7
#define ATA_CMD_IDENTIFY 0xEC

static bool ata_secondary_present = false;

static void ata_wait_bsy() {
    int timeout = 100000;
    while ((inb(ATA_STATUS) & ATA_SR_BSY) && --timeout > 0)
        ;
}

static bool ata_wait_drq() {
    int timeout = 100000;
    while (--timeout > 0) {
        uint8_t st = inb(ATA_STATUS);
        if (st & ATA_SR_ERR) return false;
        if (st & ATA_SR_DRQ) return true;
    }
    return false;
}

static void ata_select_drive(uint8_t drive, uint32_t lba) {
    // drive 0 = master, 1 = slave
    // LBA mode, bits 24-27 of LBA in lower nibble
    outb(ATA_DRIVE_SEL, 0xE0 | ((drive & 1) << 4) | ((lba >> 24) & 0x0F));
    // Small delay - read status port 4 times
    inb(ATA_STATUS); inb(ATA_STATUS); inb(ATA_STATUS); inb(ATA_STATUS);
}

bool ata_init() {
    // Select slave drive on primary channel
    outb(ATA_DRIVE_SEL, 0xB0); // Slave
    inb(ATA_STATUS); inb(ATA_STATUS); inb(ATA_STATUS); inb(ATA_STATUS);

    // Send IDENTIFY
    outb(ATA_SECT_COUNT, 0);
    outb(ATA_LBA_LO, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HI, 0);
    outb(ATA_COMMAND, ATA_CMD_IDENTIFY);

    uint8_t st = inb(ATA_STATUS);
    if (st == 0) {
        serial_write("[ATA] No slave drive on primary channel\n");
        return false;
    }

    // Wait for BSY to clear
    ata_wait_bsy();

    // Check for non-ATA device
    if (inb(ATA_LBA_MID) != 0 || inb(ATA_LBA_HI) != 0) {
        serial_write("[ATA] Slave is not ATA device\n");
        return false;
    }

    // Wait for DRQ or ERR
    int timeout = 100000;
    while (--timeout > 0) {
        st = inb(ATA_STATUS);
        if (st & ATA_SR_ERR) {
            serial_write("[ATA] Slave IDENTIFY error\n");
            return false;
        }
        if (st & ATA_SR_DRQ) break;
    }
    if (timeout <= 0) {
        serial_write("[ATA] Slave IDENTIFY timeout\n");
        return false;
    }

    // Read and discard 256 words of identify data
    for (int i = 0; i < 256; i++)
        inw(ATA_DATA);

    ata_secondary_present = true;
    serial_write("[ATA] Slave drive detected - userdata disk ready\n");
    return true;
}

bool ata_read_sector(uint32_t lba, void* buffer) {
    if (!ata_secondary_present) return false;

    ata_wait_bsy();
    ata_select_drive(1, lba); // Drive 1 = slave

    outb(ATA_SECT_COUNT, 1);
    outb(ATA_LBA_LO, lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HI, (lba >> 16) & 0xFF);
    outb(ATA_COMMAND, ATA_CMD_READ);

    ata_wait_bsy();
    if (!ata_wait_drq()) {
        serial_write("[ATA] Read sector failed\n");
        return false;
    }

    uint16_t* buf = (uint16_t*)buffer;
    for (int i = 0; i < 256; i++)
        buf[i] = inw(ATA_DATA);

    return true;
}

bool ata_write_sector(uint32_t lba, const void* buffer) {
    if (!ata_secondary_present) return false;

    ata_wait_bsy();
    ata_select_drive(1, lba); // Drive 1 = slave

    outb(ATA_SECT_COUNT, 1);
    outb(ATA_LBA_LO, lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HI, (lba >> 16) & 0xFF);
    outb(ATA_COMMAND, ATA_CMD_WRITE);

    ata_wait_bsy();
    if (!ata_wait_drq()) {
        serial_write("[ATA] Write sector failed\n");
        return false;
    }

    const uint16_t* buf = (const uint16_t*)buffer;
    for (int i = 0; i < 256; i++)
        outw(ATA_DATA, buf[i]);

    // Flush cache
    outb(ATA_COMMAND, ATA_CMD_FLUSH);
    ata_wait_bsy();

    serial_write("[ATA] Sector written OK\n");
    return true;
}
