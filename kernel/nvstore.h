#pragma once

#include "../lib/types.h"

// Persistent credential storage via ATA disk sector 0
// Layout: magic(4) + username(32) + email(64) + password(32) + valid(1) = 133 bytes

#define NVSTORE_MAGIC 0x47573243  // "GW2C"

struct UserCredentials {
    uint32_t magic;
    char username[32];
    char email[64];
    char password[32];
    uint8_t valid;       // 1 = credentials saved
    uint8_t reserved[379]; // Pad to 512 bytes (one sector)
};

// Initialize nvstore (call after ata_init)
void nvstore_init();

// Load saved credentials. Returns true if valid credentials found.
bool nvstore_load(UserCredentials* creds);

// Save credentials to disk. Returns true on success.
bool nvstore_save(const UserCredentials* creds);

// Clear saved credentials
bool nvstore_clear();
