#pragma once
#include "../lib/types.h"

struct SHA256_CTX {
    uint32_t state[8];
    uint64_t bitcount;
    uint8_t  buffer[64];
    uint32_t buflen;
};

void sha256_init(SHA256_CTX* ctx);
void sha256_update(SHA256_CTX* ctx, const void* data, uint32_t len);
void sha256_final(SHA256_CTX* ctx, uint8_t hash[32]);

// One-shot
void sha256(const void* data, uint32_t len, uint8_t hash[32]);

// HMAC-SHA256
void hmac_sha256(const void* key, int key_len,
                 const void* data, int data_len,
                 uint8_t out[32]);

// TLS PRF (SHA-256 based)
void tls_prf_sha256(const uint8_t* secret, int secret_len,
                    const char* label,
                    const uint8_t* seed, int seed_len,
                    uint8_t* out, int out_len);
