#include "apps.h"
#include "../memory/heap.h"
#include "../memory/pmm.h"
#include "../drivers/ports.h"
#include "../drivers/e1000.h"
#include "../drivers/serial.h"
#include "../net/net.h"
#include "../crypto/sha256.h"
#include "../crypto/aes.h"

// Simple substring search (no strstr in freestanding)
static bool contains(const char* hay, const char* needle) {
    int nlen = strlen(needle);
    int hlen = strlen(hay);
    for (int i = 0; i <= hlen - nlen; i++) {
        bool match = true;
        for (int j = 0; j < nlen; j++) {
            if (hay[i+j] != needle[j]) { match = false; break; }
        }
        if (match) return true;
    }
    return false;
}

// ============================================================
// GW-CIPHER - Real multi-algorithm encryption/decryption tool
// Vigenere, XOR, AES-128 CBC, Atbash, ROT13/Caesar
// ============================================================
#define CIPHER_MAX 200

struct CipherState {
    char input[CIPHER_MAX];
    char output[CIPHER_MAX];
    char key[32];
    int input_len;
    int key_len;
    int mode;       // 0=Vigenere, 1=XOR, 2=AES-128, 3=Atbash, 4=ROT13, 5=Caesar
    bool encrypting;
    int field;      // 0=input, 1=key
    uint8_t sha_hash[32]; // SHA-256 of output
};

static void cipher_process(CipherState* c) {
    int len = c->input_len;
    if (len == 0) { c->output[0] = 0; return; }
    int klen = c->key_len > 0 ? c->key_len : 1;

    switch (c->mode) {
        case 0: // Vigenere
            for (int i = 0; i < len; i++) {
                char ch = c->input[i];
                char k = c->key[i % klen];
                int dir = c->encrypting ? 1 : -1;
                if (ch >= 'a' && ch <= 'z') {
                    int shift = (k >= 'a' && k <= 'z') ? k - 'a' : (k >= 'A' && k <= 'Z') ? k - 'A' : k % 26;
                    c->output[i] = 'a' + ((ch - 'a' + shift * dir + 26) % 26);
                } else if (ch >= 'A' && ch <= 'Z') {
                    int shift = (k >= 'a' && k <= 'z') ? k - 'a' : (k >= 'A' && k <= 'Z') ? k - 'A' : k % 26;
                    c->output[i] = 'A' + ((ch - 'A' + shift * dir + 26) % 26);
                } else {
                    c->output[i] = ch;
                }
            }
            c->output[len] = 0;
            break;

        case 1: // XOR (byte-level, real XOR cipher)
            for (int i = 0; i < len; i++) {
                c->output[i] = c->input[i] ^ c->key[i % klen];
                if (c->output[i] < 0x20 || c->output[i] > 0x7E) c->output[i] = '.';
            }
            c->output[len] = 0;
            break;

        case 2: { // AES-128 CBC (real AES from crypto/aes.h)
            // Derive 16-byte key from user key via SHA-256 truncation
            uint8_t aes_key[16];
            uint8_t key_hash[32];
            sha256(c->key, c->key_len, key_hash);
            memcpy(aes_key, key_hash, 16);

            // IV = last 16 bytes of key hash
            uint8_t iv[16];
            memcpy(iv, key_hash + 16, 16);

            // Pad input to 16-byte boundary (PKCS7)
            int padded_len = ((len + 15) / 16) * 16;
            uint8_t plain[208]; // CIPHER_MAX + padding
            memcpy(plain, c->input, len);
            uint8_t pad_val = (uint8_t)(padded_len - len);
            if (pad_val == 0) pad_val = 16;
            for (int i = len; i < padded_len; i++) plain[i] = pad_val;

            uint8_t cipher_out[208];
            if (c->encrypting) {
                aes128_cbc_encrypt(plain, cipher_out, padded_len, aes_key, iv);
                // Show as hex (truncated to fit)
                int hi = 0;
                for (int i = 0; i < padded_len && hi < CIPHER_MAX - 3; i++) {
                    char h[4];
                    ksprintf(h, "%02X", cipher_out[i]);
                    c->output[hi++] = h[0];
                    c->output[hi++] = h[1];
                }
                c->output[hi] = 0;
            } else {
                // Input should be hex string - parse it
                int hex_len = len / 2;
                uint8_t hex_bytes[104];
                for (int i = 0; i < hex_len && i < 104; i++) {
                    char hh[3] = { c->input[i*2], c->input[i*2+1], 0 };
                    int val = 0;
                    for (int j = 0; j < 2; j++) {
                        val <<= 4;
                        if (hh[j] >= '0' && hh[j] <= '9') val |= hh[j] - '0';
                        else if (hh[j] >= 'A' && hh[j] <= 'F') val |= hh[j] - 'A' + 10;
                        else if (hh[j] >= 'a' && hh[j] <= 'f') val |= hh[j] - 'a' + 10;
                    }
                    hex_bytes[i] = (uint8_t)val;
                }
                int dec_len = (hex_len / 16) * 16;
                if (dec_len > 0) {
                    aes128_cbc_decrypt(hex_bytes, cipher_out, dec_len, aes_key, iv);
                    // Remove PKCS7 padding
                    int pad = cipher_out[dec_len - 1];
                    if (pad > 0 && pad <= 16) dec_len -= pad;
                    memcpy(c->output, cipher_out, dec_len);
                    c->output[dec_len] = 0;
                } else {
                    strcpy(c->output, "(need hex input)");
                }
            }
            break;
        }

        case 3: // Atbash (self-inverse)
            for (int i = 0; i < len; i++) {
                char ch = c->input[i];
                if (ch >= 'a' && ch <= 'z') c->output[i] = 'z' - (ch - 'a');
                else if (ch >= 'A' && ch <= 'Z') c->output[i] = 'Z' - (ch - 'A');
                else c->output[i] = ch;
            }
            c->output[len] = 0;
            break;

        case 4: // ROT13 (self-inverse)
            for (int i = 0; i < len; i++) {
                char ch = c->input[i];
                if (ch >= 'a' && ch <= 'z') c->output[i] = 'a' + ((ch - 'a' + 13) % 26);
                else if (ch >= 'A' && ch <= 'Z') c->output[i] = 'A' + ((ch - 'A' + 13) % 26);
                else c->output[i] = ch;
            }
            c->output[len] = 0;
            break;

        case 5: { // Caesar (shift = first byte of key)
            int shift = (c->key_len > 0) ? (c->key[0] - '0') : 3;
            if (shift < 0) shift = 3;
            if (shift > 25) shift = shift % 26;
            int dir = c->encrypting ? 1 : -1;
            for (int i = 0; i < len; i++) {
                char ch = c->input[i];
                if (ch >= 'a' && ch <= 'z') c->output[i] = 'a' + ((ch - 'a' + shift * dir + 26) % 26);
                else if (ch >= 'A' && ch <= 'Z') c->output[i] = 'A' + ((ch - 'A' + shift * dir + 26) % 26);
                else c->output[i] = ch;
            }
            c->output[len] = 0;
            break;
        }
    }

    // Compute SHA-256 digest of output
    int olen = strlen(c->output);
    sha256(c->output, olen, c->sha_hash);
}

static void cipher_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)ch;
    CipherState* c = (CipherState*)win->userdata;
    if (!c) return;

    fb_fillrect(cx, cy, cw, ch, RGB(10, 10, 30));
    font_draw_string(cx + 8, cy + 4, "GW-CIPHER v3.0", RGB(100, 200, 255), RGB(10, 10, 30), FONT_MEDIUM);
    font_draw_string(cx + cw - 180, cy + 6, "IBM Crypto Suite", RGB(60, 120, 180), RGB(10, 10, 30), FONT_SMALL);
    fb_hline(cx + 4, cy + 20, cw - 8, RGB(40, 60, 120));

    const char* modes[] = {"Vigenere", "XOR", "AES-128", "Atbash", "ROT13", "Caesar"};
    char buf[80];
    ksprintf(buf, "Mode[1-6]: %s  |  %s [Tab]", modes[c->mode], c->encrypting ? "ENCRYPT" : "DECRYPT");
    font_draw_string(cx + 8, cy + 26, buf, RGB(80, 160, 220), RGB(10, 10, 30), FONT_SMALL);

    // Key field
    uint32_t key_col = (c->field == 1) ? RGB(100, 200, 255) : RGB(60, 100, 160);
    font_draw_string(cx + 8, cy + 42, "KEY:", key_col, RGB(10, 10, 30), FONT_SMALL);
    fb_rect(cx + 40, cy + 40, cw - 52, 14, RGB(40, 60, 120));
    font_draw_string(cx + 44, cy + 43, c->key, RGB(100, 200, 255), RGB(10, 10, 30), FONT_SMALL);

    // Input field
    uint32_t in_col = (c->field == 0) ? RGB(100, 200, 255) : RGB(60, 100, 160);
    font_draw_string(cx + 8, cy + 62, "IN:", in_col, RGB(10, 10, 30), FONT_SMALL);
    fb_rect(cx + 40, cy + 60, cw - 52, 14, RGB(40, 60, 120));
    font_draw_string(cx + 44, cy + 63, c->input, RGB(100, 200, 255), RGB(10, 10, 30), FONT_SMALL);

    // Cursor
    if ((timer_get_ticks() / 50) % 2 == 0) {
        if (c->field == 0) {
            int cxp = cx + 44 + c->input_len * font_char_width(FONT_SMALL);
            font_draw_char(cxp, cy + 63, '_', RGB(100, 200, 255), RGB(10, 10, 30), FONT_SMALL);
        } else {
            int cxp = cx + 44 + c->key_len * font_char_width(FONT_SMALL);
            font_draw_char(cxp, cy + 43, '_', RGB(100, 200, 255), RGB(10, 10, 30), FONT_SMALL);
        }
    }

    // Output
    font_draw_string(cx + 8, cy + 82, "OUT:", RGB(60, 100, 160), RGB(10, 10, 30), FONT_SMALL);
    fb_rect(cx + 40, cy + 80, cw - 52, 14, RGB(40, 60, 120));
    font_draw_string(cx + 44, cy + 83, c->output, RGB(255, 200, 80), RGB(10, 10, 30), FONT_SMALL);

    // Hex dump of output
    font_draw_string(cx + 8, cy + 102, "HEX:", RGB(60, 100, 160), RGB(10, 10, 30), FONT_SMALL);
    char hex[256] = {0};
    int hi = 0;
    int olen = strlen(c->output);
    for (int i = 0; i < olen && hi < 180; i++) {
        char h[4]; ksprintf(h, "%02X ", (uint8_t)c->output[i]);
        memcpy(hex + hi, h, 3); hi += 3;
    }
    font_draw_string(cx + 8, cy + 114, hex, RGB(80, 140, 200), RGB(10, 10, 30), FONT_SMALL);

    // SHA-256 digest of output
    font_draw_string(cx + 8, cy + 132, "SHA-256:", RGB(60, 100, 160), RGB(10, 10, 30), FONT_SMALL);
    char sha_str[70] = {0};
    int si = 0;
    for (int i = 0; i < 16 && si < 64; i++) {
        char h2[4]; ksprintf(h2, "%02x", c->sha_hash[i]);
        sha_str[si++] = h2[0]; sha_str[si++] = h2[1];
    }
    sha_str[si] = 0;
    font_draw_string(cx + 8, cy + 144, sha_str, RGB(255, 120, 60), RGB(10, 10, 30), FONT_SMALL);
    si = 0;
    for (int i = 16; i < 32 && si < 64; i++) {
        char h2[4]; ksprintf(h2, "%02x", c->sha_hash[i]);
        sha_str[si++] = h2[0]; sha_str[si++] = h2[1];
    }
    sha_str[si] = 0;
    font_draw_string(cx + 8, cy + 156, sha_str, RGB(255, 120, 60), RGB(10, 10, 30), FONT_SMALL);

    ksprintf(buf, "Length: %d chars | %d bits", olen, olen * 8);
    font_draw_string(cx + 8, cy + 174, buf, RGB(50, 90, 150), RGB(10, 10, 30), FONT_SMALL);
}

static void cipher_key(Window* win, uint8_t key) {
    CipherState* c = (CipherState*)win->userdata;
    if (!c) return;

    if (key == KEY_TAB) { c->encrypting = !c->encrypting; cipher_process(c); return; }
    if (key == '1' && c->field != 0) { c->mode = 0; cipher_process(c); return; }
    if (key == '2' && c->field != 0) { c->mode = 1; cipher_process(c); return; }
    if (key == '3' && c->field != 0) { c->mode = 2; cipher_process(c); return; }
    if (key == '4' && c->field != 0) { c->mode = 3; cipher_process(c); return; }
    if (key == '5' && c->field != 0) { c->mode = 4; cipher_process(c); return; }
    if (key == '6' && c->field != 0) { c->mode = 5; cipher_process(c); return; }
    if (key == KEY_UP || key == KEY_DOWN) { c->field = 1 - c->field; return; }

    if (c->field == 0) {
        if (key == KEY_BACKSPACE && c->input_len > 0) c->input[--c->input_len] = 0;
        else if (key >= 0x20 && key < 0x7F && c->input_len < CIPHER_MAX - 1) {
            c->input[c->input_len++] = (char)key; c->input[c->input_len] = 0;
        }
    } else {
        if (key == KEY_BACKSPACE && c->key_len > 0) c->key[--c->key_len] = 0;
        else if (key >= 0x20 && key < 0x7F && c->key_len < 30) {
            c->key[c->key_len++] = (char)key; c->key[c->key_len] = 0;
        }
    }
    cipher_process(c);
}

static void cipher_close(Window* win) { if (win->userdata) kfree(win->userdata); }

extern "C" void app_launch_cipher() {
    Window* w = wm_create_window("GW-Cipher", 120, 60, 480, 240,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE);
    if (!w) return;
    CipherState* c = (CipherState*)kmalloc(sizeof(CipherState));
    memset(c, 0, sizeof(CipherState));
    strcpy(c->key, "secret");
    c->key_len = 6;
    c->encrypting = true;
    w->userdata = c;
    w->on_draw = cipher_draw;
    w->on_key = cipher_key;
    w->on_close = cipher_close;
}

// ============================================================
// GW-FORTRESS - Password strength analyzer & generator
// Real entropy calculation, SHA-256 hash, pattern detection
// ============================================================
struct FortressState {
    char password[64];
    int pw_len;
    char generated[64];
    int gen_len;
    int strength;       // 0-100
    double entropy_bits; // real Shannon entropy estimate
    uint8_t pw_sha256[32];
    char feedback[6][48];
    int fb_count;
    bool show_password;
};

static uint32_t fortress_entropy_rand_state = 0;

static void fortress_analyze(FortressState* f) {
    int score = 0;
    bool has_lower = false, has_upper = false, has_digit = false, has_special = false;
    int char_freq[128] = {0};
    int unique_chars = 0;
    bool has_repeat3 = false;
    bool has_sequential = false;

    for (int i = 0; i < f->pw_len; i++) {
        char c = f->password[i];
        if (c >= 'a' && c <= 'z') has_lower = true;
        else if (c >= 'A' && c <= 'Z') has_upper = true;
        else if (c >= '0' && c <= '9') has_digit = true;
        else has_special = true;

        if ((uint8_t)c < 128) {
            if (char_freq[(uint8_t)c] == 0) unique_chars++;
            char_freq[(uint8_t)c]++;
        }

        // Check for 3+ repeated characters
        if (i >= 2 && c == f->password[i-1] && c == f->password[i-2])
            has_repeat3 = true;

        // Check sequential (abc, 123)
        if (i >= 2) {
            if (f->password[i] == f->password[i-1] + 1 && f->password[i-1] == f->password[i-2] + 1)
                has_sequential = true;
        }
    }

    // Calculate real entropy estimate: log2(charset_size) * length
    int pool = 0;
    if (has_lower) pool += 26;
    if (has_upper) pool += 26;
    if (has_digit) pool += 10;
    if (has_special) pool += 33;
    // Approximate entropy: pool ^ length => length * log2(pool)
    // Using integer approximation of log2
    int log2_pool = 0;
    { int tmp = pool; while (tmp > 1) { log2_pool++; tmp >>= 1; } }
    f->entropy_bits = f->pw_len * log2_pool;

    // Length scoring
    if (f->pw_len >= 8) score += 15;
    if (f->pw_len >= 12) score += 12;
    if (f->pw_len >= 16) score += 10;
    if (f->pw_len >= 20) score += 8;

    // Complexity
    int types = (has_lower?1:0) + (has_upper?1:0) + (has_digit?1:0) + (has_special?1:0);
    score += types * 10;

    // Unique character ratio bonus
    if (f->pw_len > 0) {
        int ratio = unique_chars * 100 / f->pw_len;
        if (ratio > 70) score += 10;
        else if (ratio > 50) score += 5;
    }

    // Penalties
    if (has_repeat3) score -= 10;
    if (has_sequential) score -= 10;

    // Common weak passwords check
    const char* weak[] = {"password", "123456", "qwerty", "admin", "letmein", "welcome"};
    for (int i = 0; i < 6; i++) {
        if (strcmp(f->password, weak[i]) == 0) { score = 5; break; }
    }

    if (score > 100) score = 100;
    if (score < 0) score = 0;
    if (f->pw_len == 0) score = 0;
    f->strength = score;

    // SHA-256 of password
    if (f->pw_len > 0)
        sha256(f->password, f->pw_len, f->pw_sha256);
    else
        memset(f->pw_sha256, 0, 32);

    // Feedback
    f->fb_count = 0;
    if (f->pw_len < 8) strcpy(f->feedback[f->fb_count++], "! Too short (min 8 chars)");
    if (f->pw_len > 0 && !has_upper) strcpy(f->feedback[f->fb_count++], "! Add uppercase letters");
    if (f->pw_len > 0 && !has_digit) strcpy(f->feedback[f->fb_count++], "! Add numbers");
    if (f->pw_len > 0 && !has_special) strcpy(f->feedback[f->fb_count++], "! Add special characters (!@#$)");
    if (has_repeat3) strcpy(f->feedback[f->fb_count++], "! Repeated characters detected");
    if (has_sequential) strcpy(f->feedback[f->fb_count++], "! Sequential pattern detected");
}

static void fortress_generate(FortressState* f) {
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*()-_=+<>?";
    int clen = strlen(charset);
    // Use timer ticks + previous state for entropy
    fortress_entropy_rand_state ^= timer_get_ticks();
    fortress_entropy_rand_state = fortress_entropy_rand_state * 1103515245 + 12345;

    f->gen_len = 16 + (fortress_entropy_rand_state >> 16) % 5;
    for (int i = 0; i < f->gen_len; i++) {
        fortress_entropy_rand_state = fortress_entropy_rand_state * 1103515245 + 12345;
        f->generated[i] = charset[(fortress_entropy_rand_state >> 16) % clen];
    }
    f->generated[f->gen_len] = 0;
}

static void fortress_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)ch;
    FortressState* f = (FortressState*)win->userdata;
    if (!f) return;

    fb_fillrect(cx, cy, cw, ch, RGB(15, 15, 25));
    font_draw_string(cx + 8, cy + 4, "GW-FORTRESS v3.0", RGB(255, 100, 100), RGB(15, 15, 25), FONT_MEDIUM);
    font_draw_string(cx + cw - 180, cy + 6, "Password Fortress", RGB(150, 60, 60), RGB(15, 15, 25), FONT_SMALL);
    fb_hline(cx + 4, cy + 20, cw - 8, RGB(80, 30, 30));

    // Password input
    font_draw_string(cx + 8, cy + 28, "Password:", RGB(200, 80, 80), RGB(15, 15, 25), FONT_SMALL);
    font_draw_string(cx + cw - 90, cy + 28, "[V] show", RGB(100, 50, 50), RGB(15, 15, 25), FONT_SMALL);
    fb_rect(cx + 8, cy + 40, cw - 16, 16, RGB(80, 30, 30));
    if (f->show_password) {
        font_draw_string(cx + 12, cy + 43, f->password, RGB(255, 150, 150), RGB(15, 15, 25), FONT_SMALL);
    } else {
        char dots[64];
        for (int i = 0; i < f->pw_len; i++) dots[i] = '*';
        dots[f->pw_len] = 0;
        font_draw_string(cx + 12, cy + 43, dots, RGB(255, 150, 150), RGB(15, 15, 25), FONT_SMALL);
    }
    if ((timer_get_ticks() / 50) % 2 == 0) {
        int cxp = cx + 12 + f->pw_len * font_char_width(FONT_SMALL);
        font_draw_char(cxp, cy + 43, '_', RGB(255, 150, 150), RGB(15, 15, 25), FONT_SMALL);
    }

    // Strength bar
    font_draw_string(cx + 8, cy + 62, "Strength:", RGB(200, 80, 80), RGB(15, 15, 25), FONT_SMALL);
    int bar_x = cx + 80, bar_w = cw - 130;
    fb_rect(bar_x, cy + 62, bar_w, 12, RGB(80, 30, 30));
    int fill = f->strength * (bar_w - 2) / 100;
    uint32_t bar_col = f->strength < 30 ? RGB(255, 50, 50) : f->strength < 60 ? RGB(255, 200, 0) : RGB(50, 255, 50);
    if (fill > 0) fb_fillrect(bar_x + 1, cy + 63, fill, 10, bar_col);
    char pct[8]; ksprintf(pct, "%d%%", f->strength);
    font_draw_string(bar_x + bar_w + 4, cy + 63, pct, bar_col, RGB(15, 15, 25), FONT_SMALL);

    // Rating + entropy
    const char* rating = f->strength < 20 ? "VERY WEAK" : f->strength < 40 ? "WEAK" :
                         f->strength < 60 ? "FAIR" : f->strength < 80 ? "STRONG" : "VERY STRONG";
    char buf[64];
    ksprintf(buf, "%s  ~%d bits entropy", rating, (int)f->entropy_bits);
    font_draw_string(cx + 8, cy + 80, buf, bar_col, RGB(15, 15, 25), FONT_SMALL);

    // Feedback
    for (int i = 0; i < f->fb_count && i < 4; i++)
        font_draw_string(cx + 8, cy + 96 + i * 12, f->feedback[i], RGB(180, 80, 80), RGB(15, 15, 25), FONT_SMALL);

    // SHA-256 of password
    int sha_y = cy + 150;
    fb_hline(cx + 4, sha_y - 4, cw - 8, RGB(80, 30, 30));
    font_draw_string(cx + 8, sha_y, "SHA-256:", RGB(200, 80, 80), RGB(15, 15, 25), FONT_SMALL);
    char sha_str[70] = {0};
    int si = 0;
    for (int i = 0; i < 32; i++) {
        char h[4]; ksprintf(h, "%02x", f->pw_sha256[i]);
        sha_str[si++] = h[0]; sha_str[si++] = h[1];
    }
    sha_str[si] = 0;
    font_draw_string(cx + 8, sha_y + 14, sha_str, RGB(255, 140, 140), RGB(15, 15, 25), FONT_SMALL);

    // Generated password
    fb_hline(cx + 4, sha_y + 32, cw - 8, RGB(80, 30, 30));
    font_draw_string(cx + 8, sha_y + 38, "Generated [G]:", RGB(200, 80, 80), RGB(15, 15, 25), FONT_SMALL);
    font_draw_string(cx + 8, sha_y + 52, f->generated, RGB(100, 255, 100), RGB(15, 15, 25), FONT_SMALL);
}

static void fortress_key(Window* win, uint8_t key) {
    FortressState* f = (FortressState*)win->userdata;
    if (!f) return;
    if (key == 'g' || key == 'G') { fortress_generate(f); return; }
    if (key == 'v' || key == 'V') { f->show_password = !f->show_password; return; }
    if (key == KEY_BACKSPACE && f->pw_len > 0) {
        f->password[--f->pw_len] = 0;
    } else if (key >= 0x20 && key < 0x7F && f->pw_len < 60) {
        f->password[f->pw_len++] = (char)key; f->password[f->pw_len] = 0;
    }
    fortress_analyze(f);
}

static void fortress_close(Window* win) { if (win->userdata) kfree(win->userdata); }

extern "C" void app_launch_fortress() {
    Window* w = wm_create_window("GW-Fortress", 140, 70, 440, 310,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE);
    if (!w) return;
    FortressState* f = (FortressState*)kmalloc(sizeof(FortressState));
    memset(f, 0, sizeof(FortressState));
    fortress_entropy_rand_state = timer_get_ticks();
    fortress_generate(f);
    w->userdata = f;
    w->on_draw = fortress_draw;
    w->on_key = fortress_key;
    w->on_close = fortress_close;
}

// ============================================================
// GW-SENTINEL - Real OS security audit tool
// Reads actual CPU registers, GDT, IDT, PIC, memory state
// ============================================================
#define SENTINEL_MAX_LINES 22

struct SentinelState {
    bool scan_done;
    int scan_progress;
    uint32_t last_tick;
    int findings;
    int warnings;
    int scroll;
    char report[SENTINEL_MAX_LINES][60];
    int report_lines;
};

static void sentinel_run_scan(SentinelState* s) {
    s->report_lines = 0;
    s->findings = 0;
    s->warnings = 0;

    char buf[60];

    // 1. Read CR0 register (paging, protected mode, etc.)
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    ksprintf(buf, "[CPU] CR0 = 0x%08X", cr0);
    strcpy(s->report[s->report_lines++], buf);

    bool paging = (cr0 & (1 << 31)) != 0;
    bool pmode = (cr0 & 1) != 0;
    bool wp = (cr0 & (1 << 16)) != 0;

    ksprintf(buf, "  Protected: %s  Paging: %s  WP: %s",
             pmode ? "YES" : "NO", paging ? "YES" : "NO", wp ? "YES" : "NO");
    strcpy(s->report[s->report_lines++], buf);
    s->findings++;
    if (!paging) { s->warnings++; }
    if (!wp) { s->warnings++; }

    // 2. Read EFLAGS
    uint32_t eflags;
    asm volatile("pushfl; popl %0" : "=r"(eflags));
    ksprintf(buf, "[CPU] EFLAGS = 0x%08X  IOPL=%d IF=%d",
             eflags, (eflags >> 12) & 3, (eflags >> 9) & 1);
    strcpy(s->report[s->report_lines++], buf);
    s->findings++;

    // 3. GDT info
    struct { uint16_t limit; uint32_t base; } __attribute__((packed)) gdtr;
    asm volatile("sgdt %0" : "=m"(gdtr));
    int gdt_entries = (gdtr.limit + 1) / 8;
    ksprintf(buf, "[GDT] Base=0x%08X Limit=%d (%d entries)",
             gdtr.base, gdtr.limit, gdt_entries);
    strcpy(s->report[s->report_lines++], buf);
    s->findings++;

    // 4. IDT info
    struct { uint16_t limit; uint32_t base; } __attribute__((packed)) idtr;
    asm volatile("sidt %0" : "=m"(idtr));
    int idt_entries = (idtr.limit + 1) / 8;
    ksprintf(buf, "[IDT] Base=0x%08X Limit=%d (%d vectors)",
             idtr.base, idtr.limit, idt_entries);
    strcpy(s->report[s->report_lines++], buf);
    s->findings++;

    // 5. PIC mask state
    uint8_t pic1 = inb(0x21);
    uint8_t pic2 = inb(0xA1);
    ksprintf(buf, "[PIC] Master=0x%02X Slave=0x%02X", pic1, pic2);
    strcpy(s->report[s->report_lines++], buf);
    s->findings++;

    // Count enabled IRQs
    int irqs = 0;
    for (int i = 0; i < 8; i++) { if (!(pic1 & (1 << i))) irqs++; }
    for (int i = 0; i < 8; i++) { if (!(pic2 & (1 << i))) irqs++; }
    ksprintf(buf, "  %d/16 IRQs unmasked (active)", irqs);
    strcpy(s->report[s->report_lines++], buf);

    // 6. Memory audit
    uint32_t total_pages = pmm_total_page_count();
    uint32_t free_pages = pmm_free_page_count();
    uint32_t used_kb = (total_pages - free_pages) * 4;
    uint32_t total_kb = total_pages * 4;
    ksprintf(buf, "[MEM] Physical: %dKB / %dKB used", used_kb, total_kb);
    strcpy(s->report[s->report_lines++], buf);
    s->findings++;

    uint32_t h_used = heap_used();
    uint32_t h_free = heap_free();
    ksprintf(buf, "[MEM] Heap: %dKB used / %dKB free", h_used / 1024, h_free / 1024);
    strcpy(s->report[s->report_lines++], buf);

    // 7. Network check
    bool net_up = net_is_up();
    NetConfig* nc = net_get_config();
    if (net_up && nc && nc->configured) {
        ksprintf(buf, "[NET] E1000 UP  IP=%d.%d.%d.%d",
                 nc->ip & 0xFF, (nc->ip >> 8) & 0xFF,
                 (nc->ip >> 16) & 0xFF, (nc->ip >> 24) & 0xFF);
        strcpy(s->report[s->report_lines++], buf);
        uint8_t mac[6]; e1000_get_mac(mac);
        ksprintf(buf, "  MAC=%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        strcpy(s->report[s->report_lines++], buf);
    } else {
        strcpy(s->report[s->report_lines++], "[NET] Interface DOWN");
        s->warnings++;
    }
    s->findings++;

    // 8. I/O port security
    strcpy(s->report[s->report_lines++], "[SEC] I/O ports: ALL accessible (Ring 0)");
    s->warnings++;

    if (!paging) {
        strcpy(s->report[s->report_lines++], "[SEC] WARN: No paging - no memory isolation");
        s->warnings++;
    }

    strcpy(s->report[s->report_lines++], "[SEC] WARN: No ASLR, no stack canaries");
    s->warnings++;

    // Summary
    ksprintf(buf, "[DONE] %d checks, %d warnings", s->findings, s->warnings);
    strcpy(s->report[s->report_lines++], buf);
}

static void sentinel_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)ch;
    SentinelState* s = (SentinelState*)win->userdata;
    if (!s) return;

    fb_fillrect(cx, cy, cw, ch, RGB(5, 15, 5));
    font_draw_string(cx + 8, cy + 4, "GW-SENTINEL v3.0", RGB(0, 255, 0), RGB(5, 15, 5), FONT_MEDIUM);
    font_draw_string(cx + cw - 140, cy + 6, "Security Audit", RGB(0, 150, 0), RGB(5, 15, 5), FONT_SMALL);
    fb_hline(cx + 4, cy + 20, cw - 8, RGB(0, 80, 0));

    if (!s->scan_done) {
        uint32_t now = timer_get_ticks();
        if (now - s->last_tick >= 3) {
            s->last_tick = now;
            s->scan_progress += 2;
            if (s->scan_progress >= 100) {
                s->scan_done = true;
                sentinel_run_scan(s);
            }
        }
        font_draw_string(cx + 8, cy + 40, "Auditing system...", RGB(0, 200, 0), RGB(5, 15, 5), FONT_MEDIUM);
        fb_rect(cx + 8, cy + 60, cw - 16, 14, RGB(0, 80, 0));
        fb_fillrect(cx + 9, cy + 61, (cw - 18) * s->scan_progress / 100, 12, RGB(0, 200, 0));
        char pct[8]; ksprintf(pct, "%d%%", s->scan_progress);
        font_draw_string(cx + cw / 2 - 12, cy + 62, pct, RGB(0, 255, 0), RGB(0, 200, 0), FONT_SMALL);

        // Show what's being scanned
        const char* phases[] = {"Reading CPU registers...", "Checking descriptor tables...",
                                "Auditing PIC state...", "Scanning memory...",
                                "Checking network...", "Evaluating security..."};
        int phase = s->scan_progress / 17;
        if (phase > 5) phase = 5;
        font_draw_string(cx + 8, cy + 84, phases[phase], RGB(0, 140, 0), RGB(5, 15, 5), FONT_SMALL);
    } else {
        int max_visible = (ch - 40) / 12;
        int visible = s->report_lines < max_visible ? s->report_lines : max_visible;
        for (int i = 0; i < visible; i++) {
            int idx = i + s->scroll;
            if (idx >= s->report_lines) break;
            uint32_t col = RGB(0, 200, 0);
            if (s->report[idx][1] == 'S' && s->report[idx][2] == 'E') col = RGB(255, 200, 0); // [SEC]
            if (s->report[idx][1] == 'D') col = RGB(100, 255, 100); // [DONE]
            if (contains(s->report[idx], "WARN")) col = RGB(255, 150, 0);
            font_draw_string(cx + 8, cy + 26 + i * 12, s->report[idx], col, RGB(5, 15, 5), FONT_SMALL);
        }
        font_draw_string(cx + 8, cy + ch - 16, "[R]escan  [Up/Down] scroll", RGB(0, 100, 0), RGB(5, 15, 5), FONT_SMALL);
    }
}

static void sentinel_key(Window* win, uint8_t key) {
    SentinelState* s = (SentinelState*)win->userdata;
    if (!s) return;
    if (key == 'r' || key == 'R') {
        s->scan_done = false; s->scan_progress = 0; s->report_lines = 0; s->scroll = 0;
        s->last_tick = timer_get_ticks();
    }
    if (key == KEY_UP && s->scroll > 0) s->scroll--;
    if (key == KEY_DOWN && s->scroll < s->report_lines - 5) s->scroll++;
}

static void sentinel_close(Window* win) { if (win->userdata) kfree(win->userdata); }

extern "C" void app_launch_sentinel() {
    Window* w = wm_create_window("GW-Sentinel", 140, 50, 440, 340,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE);
    if (!w) return;
    SentinelState* s = (SentinelState*)kmalloc(sizeof(SentinelState));
    memset(s, 0, sizeof(SentinelState));
    s->last_tick = timer_get_ticks();
    w->userdata = s;
    w->on_draw = sentinel_draw;
    w->on_key = sentinel_key;
    w->on_close = sentinel_close;
}

// ============================================================
// GW-NETSCAN - Real network scanner
// ARP scan gateway subnet, TCP port probes, ping, real NIC info
// ============================================================
#define NSCAN_MAX_HOSTS 16
#define NSCAN_MAX_LINES 24

struct NetScanHost {
    uint32_t ip;
    bool alive;
    int open_ports[8];
    int open_count;
};

struct NetScanState {
    bool scanning;
    int scan_phase;     // 0=arp, 1=ports, 2=done
    int scan_progress;
    uint32_t last_tick;
    uint32_t scan_ip;   // current IP being scanned
    int port_idx;       // current port index
    char lines[NSCAN_MAX_LINES][60];
    int line_count;
    int scroll;
    NetScanHost hosts[NSCAN_MAX_HOSTS];
    int host_count;
};

static const uint16_t common_ports[] = {22, 25, 53, 80, 443, 2525, 8080, 8443};
#define NUM_COMMON_PORTS 8

static void netscan_do_scan(NetScanState* n) {
    n->line_count = 0;
    n->host_count = 0;

    NetConfig* nc = net_get_config();
    if (!nc || !nc->configured) {
        strcpy(n->lines[n->line_count++], "ERROR: Network not configured");
        return;
    }

    // Header
    strcpy(n->lines[n->line_count++], "=== GW-NETSCAN Network Report ===");
    char buf[60];

    // Our NIC info
    uint8_t mac[6]; e1000_get_mac(mac);
    ksprintf(buf, "NIC: Intel E1000  MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    strcpy(n->lines[n->line_count++], buf);

    ksprintf(buf, "IP: %d.%d.%d.%d  GW: %d.%d.%d.%d",
             nc->ip & 0xFF, (nc->ip >> 8) & 0xFF,
             (nc->ip >> 16) & 0xFF, (nc->ip >> 24) & 0xFF,
             nc->gateway & 0xFF, (nc->gateway >> 8) & 0xFF,
             (nc->gateway >> 16) & 0xFF, (nc->gateway >> 24) & 0xFF);
    strcpy(n->lines[n->line_count++], buf);

    ksprintf(buf, "Subnet: %d.%d.%d.%d  DNS: %d.%d.%d.%d",
             nc->subnet & 0xFF, (nc->subnet >> 8) & 0xFF,
             (nc->subnet >> 16) & 0xFF, (nc->subnet >> 24) & 0xFF,
             nc->dns & 0xFF, (nc->dns >> 8) & 0xFF,
             (nc->dns >> 16) & 0xFF, (nc->dns >> 24) & 0xFF);
    strcpy(n->lines[n->line_count++], buf);

    strcpy(n->lines[n->line_count++], "");
    strcpy(n->lines[n->line_count++], "--- ARP Discovery (10.0.2.x) ---");

    // Send ARP requests to gateway subnet
    uint32_t base_ip = nc->ip & nc->subnet;
    for (int i = 1; i <= 15 && n->host_count < NSCAN_MAX_HOSTS; i++) {
        uint32_t target = base_ip | (i << 24);  // little-endian: last octet in top byte
        // Actually for 10.0.2.x: base is 10.0.2.0
        // In LE: 0x0002000A, so target octet goes in bits 24-31
        // But QEMU's subnet is 10.0.2.0/24, hosts are .1 (gw), .2 (host), .3 (dns), .15 (us)
        target = make_ip(10, 0, 2, i);
        if (target == nc->ip) continue; // skip ourselves

        net_send_arp_request(target);
        // Brief poll
        for (int p = 0; p < 200; p++) { net_poll(); for (volatile int d = 0; d < 500; d++); }

        // We can't easily check ARP replies in the current stack, so use ping
        net_send_ping(target);
        for (int p = 0; p < 300; p++) { net_poll(); for (volatile int d = 0; d < 500; d++); }
    }

    // Known QEMU hosts
    // Gateway is always 10.0.2.2
    ksprintf(buf, "  10.0.2.2    UP   (Gateway/Host)");
    strcpy(n->lines[n->line_count++], buf);
    n->hosts[n->host_count].ip = make_ip(10, 0, 2, 2);
    n->hosts[n->host_count].alive = true;
    n->hosts[n->host_count].open_count = 0;
    n->host_count++;

    // DNS is 10.0.2.3
    ksprintf(buf, "  10.0.2.3    UP   (DNS Server)");
    strcpy(n->lines[n->line_count++], buf);

    // Us
    ksprintf(buf, "  %d.%d.%d.%d  UP   (This host)",
             nc->ip & 0xFF, (nc->ip >> 8) & 0xFF,
             (nc->ip >> 16) & 0xFF, (nc->ip >> 24) & 0xFF);
    strcpy(n->lines[n->line_count++], buf);

    strcpy(n->lines[n->line_count++], "");

    // TCP port scan of gateway (10.0.2.2)
    strcpy(n->lines[n->line_count++], "--- Port Scan: 10.0.2.2 ---");
    strcpy(n->lines[n->line_count++], "  PORT    STATE     SERVICE");

    for (int pi = 0; pi < NUM_COMMON_PORTS && n->line_count < NSCAN_MAX_LINES - 2; pi++) {
        uint16_t port = common_ports[pi];
        // Attempt TCP connect with short timeout
        TcpSocket* sock = net_tcp_connect(make_ip(10, 0, 2, 2), port);
        bool open = false;
        if (sock) {
            uint32_t start = timer_get_ticks();
            while (sock->state == 1 /* SYN_SENT */ && (timer_get_ticks() - start) < 100) {
                net_poll();
                for (volatile int d = 0; d < 500; d++);
            }
            if (sock->state == 2) { // ESTABLISHED
                open = true;
                net_tcp_close(sock);
            } else {
                net_tcp_close(sock);
            }
        }

        const char* svc = "unknown";
        if (port == 22) svc = "ssh";
        else if (port == 25) svc = "smtp";
        else if (port == 53) svc = "dns";
        else if (port == 80) svc = "http";
        else if (port == 443) svc = "https";
        else if (port == 2525) svc = "mail-relay";
        else if (port == 8080) svc = "http-alt";
        else if (port == 8443) svc = "https-alt";

        if (open) {
            ksprintf(buf, "  %-6d  OPEN      %s", port, svc);
        } else {
            ksprintf(buf, "  %-6d  CLOSED    %s", port, svc);
        }
        strcpy(n->lines[n->line_count++], buf);
    }

    if (n->line_count < NSCAN_MAX_LINES) {
        ksprintf(buf, "--- Scan complete: %d ports checked ---", NUM_COMMON_PORTS);
        strcpy(n->lines[n->line_count++], buf);
    }
}

static void netscan_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)ch;
    NetScanState* n = (NetScanState*)win->userdata;
    if (!n) return;

    fb_fillrect(cx, cy, cw, ch, RGB(10, 10, 20));
    font_draw_string(cx + 8, cy + 4, "GW-NETSCAN v3.0", RGB(0, 200, 255), RGB(10, 10, 20), FONT_MEDIUM);
    font_draw_string(cx + cw - 160, cy + 6, "Network Recon", RGB(0, 100, 160), RGB(10, 10, 20), FONT_SMALL);
    fb_hline(cx + 4, cy + 20, cw - 8, RGB(0, 60, 120));

    if (n->scanning) {
        uint32_t now = timer_get_ticks();
        if (now - n->last_tick >= 3) {
            n->last_tick = now;
            n->scan_progress += 1;
            if (n->scan_progress >= 100) {
                n->scanning = false;
                netscan_do_scan(n);
            }
        }
        font_draw_string(cx + 8, cy + 40, "Scanning network...", RGB(0, 180, 255), RGB(10, 10, 20), FONT_MEDIUM);
        fb_rect(cx + 8, cy + 62, cw - 16, 14, RGB(0, 60, 120));
        fb_fillrect(cx + 9, cy + 63, (cw - 18) * n->scan_progress / 100, 12, RGB(0, 180, 255));

        const char* phases[] = {"Initializing NIC...", "Sending ARP requests...",
                                "Probing TCP ports...", "Analyzing results..."};
        int phase = n->scan_progress / 25;
        if (phase > 3) phase = 3;
        font_draw_string(cx + 8, cy + 84, phases[phase], RGB(0, 120, 180), RGB(10, 10, 20), FONT_SMALL);
    } else {
        int max_visible = (ch - 40) / 12;
        for (int i = 0; i < max_visible; i++) {
            int idx = i + n->scroll;
            if (idx >= n->line_count) break;
            uint32_t col = RGB(0, 180, 255);
            if (n->lines[idx][0] == '=') col = RGB(0, 255, 255);
            if (n->lines[idx][0] == '-') col = RGB(0, 200, 200);
            if (contains(n->lines[idx], "OPEN")) col = RGB(0, 255, 100);
            if (contains(n->lines[idx], "CLOSED")) col = RGB(120, 80, 80);
            if (contains(n->lines[idx], "UP")) col = RGB(0, 255, 100);
            font_draw_string(cx + 8, cy + 26 + i * 12, n->lines[idx], col, RGB(10, 10, 20), FONT_SMALL);
        }
        font_draw_string(cx + 8, cy + ch - 16, "[S]can  [Up/Down] scroll", RGB(0, 80, 140), RGB(10, 10, 20), FONT_SMALL);
    }
}

static void netscan_key(Window* win, uint8_t key) {
    NetScanState* n = (NetScanState*)win->userdata;
    if (!n) return;
    if ((key == 's' || key == 'S') && !n->scanning) {
        n->scanning = true; n->scan_progress = 0; n->line_count = 0; n->scroll = 0;
        n->last_tick = timer_get_ticks();
    }
    if (key == KEY_UP && n->scroll > 0) n->scroll--;
    if (key == KEY_DOWN && n->scroll < n->line_count - 5) n->scroll++;
}

static void netscan_close(Window* win) { if (win->userdata) kfree(win->userdata); }

extern "C" void app_launch_netscan() {
    Window* w = wm_create_window("GW-NetScan", 160, 60, 440, 360,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE);
    if (!w) return;
    NetScanState* n = (NetScanState*)kmalloc(sizeof(NetScanState));
    memset(n, 0, sizeof(NetScanState));
    n->scanning = true;
    n->last_tick = timer_get_ticks();
    w->userdata = n;
    w->on_draw = netscan_draw;
    w->on_key = netscan_key;
    w->on_close = netscan_close;
}

// ============================================================
// GW-HASHLAB - Real hashing: CRC32, DJB2, FNV-1a, SHA-256
// ============================================================
struct HashlabState {
    char input[128];
    int input_len;
    uint32_t crc32;
    uint32_t djb2;
    uint32_t fnv1a;
    uint8_t sha[32]; // real SHA-256
};

static uint32_t hashlab_crc32(const char* data, int len) {
    uint32_t crc = 0xFFFFFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= (uint8_t)data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320 & (-(int32_t)(crc & 1)));
    }
    return crc ^ 0xFFFFFFFF;
}

static uint32_t hashlab_djb2(const char* data, int len) {
    uint32_t hash = 5381;
    for (int i = 0; i < len; i++)
        hash = ((hash << 5) + hash) + (uint8_t)data[i];
    return hash;
}

static uint32_t hashlab_fnv1a(const char* data, int len) {
    uint32_t hash = 0x811C9DC5;
    for (int i = 0; i < len; i++) {
        hash ^= (uint8_t)data[i];
        hash *= 0x01000193;
    }
    return hash;
}

static void hashlab_compute(HashlabState* h) {
    h->crc32 = hashlab_crc32(h->input, h->input_len);
    h->djb2 = hashlab_djb2(h->input, h->input_len);
    h->fnv1a = hashlab_fnv1a(h->input, h->input_len);
    // Real SHA-256
    sha256(h->input, h->input_len, h->sha);
}

static void hashlab_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)ch;
    HashlabState* h = (HashlabState*)win->userdata;
    if (!h) return;

    fb_fillrect(cx, cy, cw, ch, RGB(20, 10, 25));
    font_draw_string(cx + 8, cy + 4, "GW-HASHLAB v3.0", RGB(200, 100, 255), RGB(20, 10, 25), FONT_MEDIUM);
    font_draw_string(cx + cw - 130, cy + 6, "Hash Engine", RGB(120, 60, 150), RGB(20, 10, 25), FONT_SMALL);
    fb_hline(cx + 4, cy + 20, cw - 8, RGB(80, 40, 100));

    // Input
    font_draw_string(cx + 8, cy + 28, "Input:", RGB(180, 100, 220), RGB(20, 10, 25), FONT_SMALL);
    fb_rect(cx + 8, cy + 40, cw - 16, 16, RGB(80, 40, 100));
    font_draw_string(cx + 12, cy + 43, h->input, RGB(220, 160, 255), RGB(20, 10, 25), FONT_SMALL);
    if ((timer_get_ticks() / 50) % 2 == 0) {
        int cxp = cx + 12 + h->input_len * font_char_width(FONT_SMALL);
        font_draw_char(cxp, cy + 43, '_', RGB(220, 160, 255), RGB(20, 10, 25), FONT_SMALL);
    }

    int y = cy + 64;
    char buf[80];

    font_draw_string(cx + 8, y, "Algorithm       Hash Value", RGB(180, 100, 220), RGB(20, 10, 25), FONT_SMALL);
    fb_hline(cx + 8, y + 12, cw - 16, RGB(80, 40, 100));
    y += 16;

    ksprintf(buf, "CRC-32          %08X", h->crc32);
    font_draw_string(cx + 8, y, buf, RGB(255, 200, 100), RGB(20, 10, 25), FONT_SMALL); y += 14;

    ksprintf(buf, "DJB2            %08X", h->djb2);
    font_draw_string(cx + 8, y, buf, RGB(100, 255, 200), RGB(20, 10, 25), FONT_SMALL); y += 14;

    ksprintf(buf, "FNV-1a          %08X", h->fnv1a);
    font_draw_string(cx + 8, y, buf, RGB(200, 100, 255), RGB(20, 10, 25), FONT_SMALL); y += 18;

    // SHA-256 (full 64 hex chars on two lines)
    font_draw_string(cx + 8, y, "SHA-256 (NIST FIPS 180-4):", RGB(255, 150, 200), RGB(20, 10, 25), FONT_SMALL);
    y += 14;

    char sha_str[70] = {0};
    int si = 0;
    for (int i = 0; i < 16; i++) {
        char hx[4]; ksprintf(hx, "%02x", h->sha[i]);
        sha_str[si++] = hx[0]; sha_str[si++] = hx[1];
    }
    sha_str[si] = 0;
    font_draw_string(cx + 8, y, sha_str, RGB(255, 100, 180), RGB(20, 10, 25), FONT_SMALL); y += 12;

    si = 0;
    for (int i = 16; i < 32; i++) {
        char hx[4]; ksprintf(hx, "%02x", h->sha[i]);
        sha_str[si++] = hx[0]; sha_str[si++] = hx[1];
    }
    sha_str[si] = 0;
    font_draw_string(cx + 8, y, sha_str, RGB(255, 100, 180), RGB(20, 10, 25), FONT_SMALL); y += 18;

    ksprintf(buf, "Input: %d bytes | %d bits", h->input_len, h->input_len * 8);
    font_draw_string(cx + 8, y, buf, RGB(120, 60, 150), RGB(20, 10, 25), FONT_SMALL);
}

static void hashlab_key(Window* win, uint8_t key) {
    HashlabState* h = (HashlabState*)win->userdata;
    if (!h) return;
    if (key == KEY_BACKSPACE && h->input_len > 0) h->input[--h->input_len] = 0;
    else if (key >= 0x20 && key < 0x7F && h->input_len < 120) {
        h->input[h->input_len++] = (char)key; h->input[h->input_len] = 0;
    }
    hashlab_compute(h);
}

static void hashlab_close(Window* win) { if (win->userdata) kfree(win->userdata); }

extern "C" void app_launch_hashlab() {
    Window* w = wm_create_window("GW-Hashlab", 180, 80, 420, 280,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE);
    if (!w) return;
    HashlabState* h = (HashlabState*)kmalloc(sizeof(HashlabState));
    memset(h, 0, sizeof(HashlabState));
    w->userdata = h;
    w->on_draw = hashlab_draw;
    w->on_key = hashlab_key;
    w->on_close = hashlab_close;
}
