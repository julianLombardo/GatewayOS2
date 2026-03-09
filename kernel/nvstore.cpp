#include "nvstore.h"
#include "../drivers/ata.h"
#include "../drivers/serial.h"
#include "../lib/string.h"
#include "../lib/printf.h"

static bool nvstore_available = false;

void nvstore_init() {
    nvstore_available = ata_init();
    if (nvstore_available)
        serial_write("[NVSTORE] Persistent storage ready\n");
    else
        serial_write("[NVSTORE] No persistent storage available\n");
}

bool nvstore_load(UserCredentials* creds) {
    if (!nvstore_available) return false;

    uint8_t sector[512];
    if (!ata_read_sector(0, sector)) return false;

    memcpy(creds, sector, sizeof(UserCredentials));

    if (creds->magic != NVSTORE_MAGIC || creds->valid != 1) {
        serial_write("[NVSTORE] No saved credentials found\n");
        return false;
    }

    // Null-terminate safety
    creds->username[31] = 0;
    creds->email[63] = 0;
    creds->password[31] = 0;

    char buf[80];
    ksprintf(buf, "[NVSTORE] Loaded credentials for user: %s\n", creds->username);
    serial_write(buf);
    return true;
}

bool nvstore_save(const UserCredentials* creds) {
    if (!nvstore_available) return false;

    uint8_t sector[512];
    memset(sector, 0, 512);
    memcpy(sector, creds, sizeof(UserCredentials));

    if (!ata_write_sector(0, sector)) return false;

    serial_write("[NVSTORE] Credentials saved to disk\n");
    return true;
}

bool nvstore_clear() {
    if (!nvstore_available) return false;

    uint8_t sector[512];
    memset(sector, 0, 512);

    if (!ata_write_sector(0, sector)) return false;

    serial_write("[NVSTORE] Credentials cleared\n");
    return true;
}
