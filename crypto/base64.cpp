#include "base64.h"

static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

int base64_encode(const void* data, int len, char* out, int out_max) {
    const uint8_t* d = (const uint8_t*)data;
    int o = 0;
    for (int i = 0; i < len; i += 3) {
        if (o + 4 >= out_max) break;
        uint32_t v = d[i] << 16;
        if (i + 1 < len) v |= d[i + 1] << 8;
        if (i + 2 < len) v |= d[i + 2];
        out[o++] = b64_table[(v >> 18) & 0x3F];
        out[o++] = b64_table[(v >> 12) & 0x3F];
        out[o++] = (i + 1 < len) ? b64_table[(v >> 6) & 0x3F] : '=';
        out[o++] = (i + 2 < len) ? b64_table[v & 0x3F] : '=';
    }
    out[o] = 0;
    return o;
}

int base64_decode(const char* in, int len, uint8_t* out, int out_max) {
    int o = 0;
    for (int i = 0; i < len; i += 4) {
        int a = b64_val(in[i]), b = b64_val(in[i + 1]);
        int c = (i + 2 < len && in[i + 2] != '=') ? b64_val(in[i + 2]) : 0;
        int d = (i + 3 < len && in[i + 3] != '=') ? b64_val(in[i + 3]) : 0;
        if (a < 0 || b < 0) break;
        uint32_t v = (a << 18) | (b << 12) | (c << 6) | d;
        if (o < out_max) out[o++] = (v >> 16) & 0xFF;
        if (o < out_max && in[i + 2] != '=') out[o++] = (v >> 8) & 0xFF;
        if (o < out_max && in[i + 3] != '=') out[o++] = v & 0xFF;
    }
    return o;
}
