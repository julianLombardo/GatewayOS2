#pragma once
#include "../lib/types.h"

// AES-128 block cipher
void aes128_encrypt_block(const uint8_t in[16], uint8_t out[16], const uint8_t key[16]);
void aes128_decrypt_block(const uint8_t in[16], uint8_t out[16], const uint8_t key[16]);

// AES-128-CBC
void aes128_cbc_encrypt(const uint8_t* in, uint8_t* out, int len,
                        const uint8_t key[16], uint8_t iv[16]);
void aes128_cbc_decrypt(const uint8_t* in, uint8_t* out, int len,
                        const uint8_t key[16], uint8_t iv[16]);
