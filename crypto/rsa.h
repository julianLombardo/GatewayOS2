#pragma once
#include "../lib/types.h"

// Big number: up to 512 bytes (4096 bits) stored big-endian
#define BN_MAX_BYTES 512

struct BigNum {
    uint8_t d[BN_MAX_BYTES]; // big-endian
    int len;                  // actual byte length
};

void bn_from_bytes(BigNum* n, const uint8_t* data, int len);
void bn_to_bytes(const BigNum* n, uint8_t* out, int out_len);
int  bn_cmp(const BigNum* a, const BigNum* b);
void bn_mod_exp(BigNum* result, const BigNum* base, const BigNum* exp, const BigNum* mod);

// RSA public key encrypt (PKCS#1 v1.5 padding)
// Encrypts data with public key (n, e) — used for pre-master secret
int rsa_encrypt(const uint8_t* data, int data_len,
                const uint8_t* n, int n_len,
                const uint8_t* e, int e_len,
                uint8_t* out, int out_max);
