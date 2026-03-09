#pragma once

#include "../lib/types.h"

// Minimal ATA PIO driver for reading/writing raw sectors on primary IDE
// Used for persistent user data storage via a small disk image

// Initialize ATA - detect if secondary drive (0x81) exists
bool ata_init();

// Read a single 512-byte sector from drive 1 (secondary on primary channel)
// Returns true on success
bool ata_read_sector(uint32_t lba, void* buffer);

// Write a single 512-byte sector to drive 1
// Returns true on success
bool ata_write_sector(uint32_t lba, const void* buffer);
