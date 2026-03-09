#include "rsa.h"
#include "../lib/string.h"
#include "../lib/printf.h"
#include "../drivers/serial.h"

// We use a compact representation: work with uint32_t words for speed
// Max 128 words = 512 bytes = 4096 bits
#define BN_WORDS 128

struct BN {
    uint32_t w[BN_WORDS]; // little-endian words
    int n;                // number of significant words
};

static void bn_zero(BN* a) { memset(a->w, 0, sizeof(a->w)); a->n = 1; }

static void bn_from_be(BN* a, const uint8_t* data, int len) {
    bn_zero(a);
    // Skip leading zeros
    while (len > 1 && data[0] == 0) { data++; len--; }
    a->n = (len + 3) / 4;
    if (a->n > BN_WORDS) a->n = BN_WORDS;
    for (int i = 0; i < len; i++) {
        int word_idx = (len - 1 - i) / 4;
        int byte_idx = (len - 1 - i) % 4;
        if (word_idx < BN_WORDS)
            a->w[word_idx] |= (uint32_t)data[i] << (byte_idx * 8);
    }
    // Trim
    while (a->n > 1 && a->w[a->n - 1] == 0) a->n--;
}

static void bn_to_be(const BN* a, uint8_t* out, int out_len) {
    memset(out, 0, out_len);
    for (int i = 0; i < out_len; i++) {
        int word_idx = (out_len - 1 - i) / 4;
        int byte_idx = (out_len - 1 - i) % 4;
        if (word_idx < a->n)
            out[i] = (a->w[word_idx] >> (byte_idx * 8)) & 0xFF;
    }
}

static int bn_cmp_bn(const BN* a, const BN* b) {
    int max_n = a->n > b->n ? a->n : b->n;
    for (int i = max_n - 1; i >= 0; i--) {
        uint32_t aw = (i < a->n) ? a->w[i] : 0;
        uint32_t bw = (i < b->n) ? b->w[i] : 0;
        if (aw < bw) return -1;
        if (aw > bw) return 1;
    }
    return 0;
}

// a = a + b
static void bn_add_bn(BN* a, const BN* b) {
    uint64_t carry = 0;
    int max_n = a->n > b->n ? a->n : b->n;
    for (int i = 0; i < max_n || carry; i++) {
        if (i >= BN_WORDS) break;
        uint64_t sum = carry;
        sum += (i < a->n) ? a->w[i] : 0;
        sum += (i < b->n) ? b->w[i] : 0;
        a->w[i] = (uint32_t)sum;
        carry = sum >> 32;
    }
    a->n = max_n;
    if (a->n < BN_WORDS && a->w[a->n]) a->n++;
    while (a->n > 1 && a->w[a->n - 1] == 0) a->n--;
}

// a = a - b (assumes a >= b)
static void bn_sub_bn(BN* a, const BN* b) {
    int64_t borrow = 0;
    for (int i = 0; i < a->n; i++) {
        int64_t diff = (int64_t)a->w[i] - ((i < b->n) ? b->w[i] : 0) - borrow;
        if (diff < 0) { diff += (1LL << 32); borrow = 1; }
        else borrow = 0;
        a->w[i] = (uint32_t)diff;
    }
    while (a->n > 1 && a->w[a->n - 1] == 0) a->n--;
}

// result = a * b (full multiply, no mod)
static void bn_mul(BN* result, const BN* a, const BN* b) {
    bn_zero(result);
    for (int i = 0; i < a->n; i++) {
        if (a->w[i] == 0) continue;
        uint64_t carry = 0;
        for (int j = 0; j < b->n; j++) {
            int k = i + j;
            if (k >= BN_WORDS) break;
            uint64_t prod = (uint64_t)a->w[i] * b->w[j] + result->w[k] + carry;
            result->w[k] = (uint32_t)prod;
            carry = prod >> 32;
        }
        int k = i + b->n;
        if (k < BN_WORDS) result->w[k] += (uint32_t)carry;
    }
    result->n = a->n + b->n;
    if (result->n > BN_WORDS) result->n = BN_WORDS;
    while (result->n > 1 && result->w[result->n - 1] == 0) result->n--;
}

// Shift left by 1 word (multiply by 2^32)
static void bn_shl_word(BN* a) {
    if (a->n >= BN_WORDS) return;
    for (int i = a->n; i > 0; i--)
        a->w[i] = a->w[i - 1];
    a->w[0] = 0;
    a->n++;
}

// result = a mod m using repeated subtraction with shifting
// This is essentially long division
static void bn_mod(BN* result, const BN* a, const BN* m) {
    BN r;
    memcpy(&r, a, sizeof(BN));

    // Shift m up to align with top of r
    int shift = r.n - m->n;
    if (shift < 0) { memcpy(result, a, sizeof(BN)); return; }

    BN shifted_m;
    memcpy(&shifted_m, m, sizeof(BN));
    // Shift left by 'shift' words
    if (shift > 0) {
        for (int i = shifted_m.n - 1 + shift; i >= shift; i--) {
            if (i < BN_WORDS) shifted_m.w[i] = shifted_m.w[i - shift];
        }
        for (int i = 0; i < shift; i++) shifted_m.w[i] = 0;
        shifted_m.n += shift;
        if (shifted_m.n > BN_WORDS) shifted_m.n = BN_WORDS;
    }

    for (int s = shift; s >= 0; s--) {
        // Trial subtract: while r >= shifted_m, do r -= shifted_m
        // Use quotient estimation for speed
        while (bn_cmp_bn(&r, &shifted_m) >= 0) {
            bn_sub_bn(&r, &shifted_m);
        }
        // Shift m right by 1 word
        if (s > 0) {
            for (int i = 0; i < shifted_m.n - 1; i++)
                shifted_m.w[i] = shifted_m.w[i + 1];
            shifted_m.w[shifted_m.n - 1] = 0;
            shifted_m.n--;
            while (shifted_m.n > 1 && shifted_m.w[shifted_m.n - 1] == 0) shifted_m.n--;
        }
    }

    memcpy(result, &r, sizeof(BN));
}

// result = (a * b) mod m
static void bn_mulmod(BN* result, const BN* a, const BN* b, const BN* m) {
    static BN prod;
    bn_mul(&prod, a, b);
    bn_mod(result, &prod, m);
}

// result = base^exp mod m (square-and-multiply)
static void bn_modexp(BN* result, const BN* base, const BN* exp, const BN* m) {
    BN r, b;
    bn_zero(&r);
    r.w[0] = 1; // r = 1
    memcpy(&b, base, sizeof(BN));

    // Reduce base mod m first
    BN b_reduced;
    bn_mod(&b_reduced, &b, m);

    // Find highest set bit in exponent
    int top_word = exp->n - 1;
    while (top_word > 0 && exp->w[top_word] == 0) top_word--;

    int total_bits = 0;
    for (int i = top_word; i >= 0; i--)
        for (int bit = 31; bit >= 0; bit--)
            if (exp->w[i] & (1U << bit)) { total_bits = i * 32 + bit + 1; goto found; }
    found:

    serial_write("[RSA] Computing modexp...\n");

    int bit_count = 0;
    bool started = false;
    for (int i = top_word; i >= 0; i--) {
        for (int bit = 31; bit >= 0; bit--) {
            if (!started) {
                if (exp->w[i] & (1U << bit)) {
                    started = true;
                    memcpy(&r, &b_reduced, sizeof(BN));
                }
                continue;
            }
            // Square
            bn_mulmod(&r, &r, &r, m);
            bit_count++;
            // Multiply if bit set
            if (exp->w[i] & (1U << bit)) {
                bn_mulmod(&r, &r, &b_reduced, m);
                bit_count++;
            }
        }
    }

    char dbg[48];
    ksprintf(dbg, "[RSA] Done, %d mulmods\n", bit_count);
    serial_write(dbg);

    memcpy(result, &r, sizeof(BN));
}

// === Public API (adapts BigNum ↔ BN) ===

void bn_from_bytes(BigNum* n, const uint8_t* data, int len) {
    while (len > 1 && data[0] == 0) { data++; len--; }
    if (len > BN_MAX_BYTES) len = BN_MAX_BYTES;
    memset(n->d, 0, BN_MAX_BYTES);
    memcpy(n->d + BN_MAX_BYTES - len, data, len);
    n->len = len;
}

void bn_to_bytes(const BigNum* n, uint8_t* out, int out_len) {
    int start = BN_MAX_BYTES - out_len;
    if (start < 0) start = 0;
    int pad = out_len - (BN_MAX_BYTES - start);
    if (pad > 0) { memset(out, 0, pad); out += pad; out_len -= pad; }
    memcpy(out, n->d + start, out_len);
}

int bn_cmp(const BigNum* a, const BigNum* b) {
    for (int i = 0; i < BN_MAX_BYTES; i++) {
        if (a->d[i] < b->d[i]) return -1;
        if (a->d[i] > b->d[i]) return 1;
    }
    return 0;
}

void bn_mod_exp(BigNum* result, const BigNum* base, const BigNum* exp, const BigNum* mod) {
    BN b, e, m, r;
    bn_from_be(&b, base->d + BN_MAX_BYTES - base->len, base->len);
    bn_from_be(&e, exp->d + BN_MAX_BYTES - exp->len, exp->len);
    bn_from_be(&m, mod->d + BN_MAX_BYTES - mod->len, mod->len);
    bn_modexp(&r, &b, &e, &m);
    // Convert back
    memset(result->d, 0, BN_MAX_BYTES);
    bn_to_be(&r, result->d + BN_MAX_BYTES - mod->len, mod->len);
    result->len = mod->len;
}

// Simple PRNG for PKCS#1 padding
static uint32_t rsa_rand_state = 0x12345678;
static uint8_t rsa_rand_byte() {
    rsa_rand_state = rsa_rand_state * 1103515245 + 12345;
    uint8_t v = (rsa_rand_state >> 16) & 0xFF;
    return v ? v : 1;
}

int rsa_encrypt(const uint8_t* data, int data_len,
                const uint8_t* n_bytes, int n_len,
                const uint8_t* e_bytes, int e_len,
                uint8_t* out, int out_max) {
    if (data_len > n_len - 11) return -1;
    if (out_max < n_len) return -1;

    for (int i = 0; i < data_len; i++)
        rsa_rand_state ^= ((uint32_t)data[i] << ((i % 4) * 8));

    // PKCS#1 v1.5 padding
    uint8_t padded[BN_MAX_BYTES];
    memset(padded, 0, sizeof(padded));
    int pad_len = n_len - data_len - 3;
    padded[0] = 0x00;
    padded[1] = 0x02;
    for (int i = 0; i < pad_len; i++)
        padded[2 + i] = rsa_rand_byte();
    padded[2 + pad_len] = 0x00;
    memcpy(padded + 3 + pad_len, data, data_len);

    BigNum msg, exp, mod, result;
    bn_from_bytes(&msg, padded, n_len);
    bn_from_bytes(&exp, e_bytes, e_len);
    bn_from_bytes(&mod, n_bytes, n_len);

    serial_write("[RSA] Encrypting pre-master secret...\n");
    bn_mod_exp(&result, &msg, &exp, &mod);
    serial_write("[RSA] Encryption complete\n");

    bn_to_bytes(&result, out, n_len);
    return n_len;
}
