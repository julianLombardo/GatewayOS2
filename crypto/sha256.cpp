#include "sha256.h"
#include "../lib/string.h"

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x,y,z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x)  (ROR(x, 2) ^ ROR(x,13) ^ ROR(x,22))
#define EP1(x)  (ROR(x, 6) ^ ROR(x,11) ^ ROR(x,25))
#define SIG0(x) (ROR(x, 7) ^ ROR(x,18) ^ ((x) >> 3))
#define SIG1(x) (ROR(x,17) ^ ROR(x,19) ^ ((x) >> 10))

static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static void sha256_transform(SHA256_CTX* ctx, const uint8_t block[64]) {
    uint32_t W[64], a, b, c, d, e, f, g, h, t1, t2;

    for (int i = 0; i < 16; i++)
        W[i] = (block[i*4] << 24) | (block[i*4+1] << 16) | (block[i*4+2] << 8) | block[i*4+3];
    for (int i = 16; i < 64; i++)
        W[i] = SIG1(W[i-2]) + W[i-7] + SIG0(W[i-15]) + W[i-16];

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (int i = 0; i < 64; i++) {
        t1 = h + EP1(e) + CH(e,f,g) + K[i] + W[i];
        t2 = EP0(a) + MAJ(a,b,c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

void sha256_init(SHA256_CTX* ctx) {
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
    ctx->bitcount = 0;
    ctx->buflen = 0;
}

void sha256_update(SHA256_CTX* ctx, const void* data, uint32_t len) {
    const uint8_t* d = (const uint8_t*)data;
    for (uint32_t i = 0; i < len; i++) {
        ctx->buffer[ctx->buflen++] = d[i];
        if (ctx->buflen == 64) {
            sha256_transform(ctx, ctx->buffer);
            ctx->bitcount += 512;
            ctx->buflen = 0;
        }
    }
}

void sha256_final(SHA256_CTX* ctx, uint8_t hash[32]) {
    ctx->bitcount += ctx->buflen * 8;
    ctx->buffer[ctx->buflen++] = 0x80;
    if (ctx->buflen > 56) {
        while (ctx->buflen < 64) ctx->buffer[ctx->buflen++] = 0;
        sha256_transform(ctx, ctx->buffer);
        ctx->buflen = 0;
    }
    while (ctx->buflen < 56) ctx->buffer[ctx->buflen++] = 0;
    for (int i = 7; i >= 0; i--)
        ctx->buffer[ctx->buflen++] = (ctx->bitcount >> (i * 8)) & 0xFF;
    sha256_transform(ctx, ctx->buffer);

    for (int i = 0; i < 8; i++) {
        hash[i*4]   = (ctx->state[i] >> 24) & 0xFF;
        hash[i*4+1] = (ctx->state[i] >> 16) & 0xFF;
        hash[i*4+2] = (ctx->state[i] >>  8) & 0xFF;
        hash[i*4+3] =  ctx->state[i]        & 0xFF;
    }
}

void sha256(const void* data, uint32_t len, uint8_t hash[32]) {
    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, hash);
}

void hmac_sha256(const void* key, int key_len,
                 const void* data, int data_len,
                 uint8_t out[32]) {
    uint8_t k_pad[64];
    uint8_t tk[32];

    // If key > 64 bytes, hash it
    if (key_len > 64) {
        sha256(key, key_len, tk);
        key = tk;
        key_len = 32;
    }

    // ipad
    memset(k_pad, 0x36, 64);
    for (int i = 0; i < key_len; i++)
        k_pad[i] ^= ((const uint8_t*)key)[i];

    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, k_pad, 64);
    sha256_update(&ctx, data, data_len);
    uint8_t inner[32];
    sha256_final(&ctx, inner);

    // opad
    memset(k_pad, 0x5c, 64);
    for (int i = 0; i < key_len; i++)
        k_pad[i] ^= ((const uint8_t*)key)[i];

    sha256_init(&ctx);
    sha256_update(&ctx, k_pad, 64);
    sha256_update(&ctx, inner, 32);
    sha256_final(&ctx, out);
}

void tls_prf_sha256(const uint8_t* secret, int secret_len,
                    const char* label,
                    const uint8_t* seed, int seed_len,
                    uint8_t* out, int out_len) {
    // P_SHA256(secret, label + seed)
    int label_len = 0;
    while (label[label_len]) label_len++;

    // Concatenate label + seed
    uint8_t ls[128];
    int ls_len = 0;
    memcpy(ls, label, label_len); ls_len += label_len;
    memcpy(ls + ls_len, seed, seed_len); ls_len += seed_len;

    // A(0) = label + seed
    // A(i) = HMAC(secret, A(i-1))
    uint8_t A[32];
    hmac_sha256(secret, secret_len, ls, ls_len, A); // A(1)

    int pos = 0;
    while (pos < out_len) {
        // HMAC(secret, A(i) + label + seed)
        uint8_t buf[32 + 128];
        memcpy(buf, A, 32);
        memcpy(buf + 32, ls, ls_len);
        uint8_t p[32];
        hmac_sha256(secret, secret_len, buf, 32 + ls_len, p);

        int copy = out_len - pos;
        if (copy > 32) copy = 32;
        memcpy(out + pos, p, copy);
        pos += copy;

        // A(i+1) = HMAC(secret, A(i))
        hmac_sha256(secret, secret_len, A, 32, A);
    }
}
