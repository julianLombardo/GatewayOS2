#include "printf.h"
#include "string.h"
#include "../drivers/serial.h"

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)

static void put_uint(char** buf, uint32_t val, int base, int width, char pad, bool upper) {
    char tmp[12];
    int i = 0;
    const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (val == 0) { tmp[i++] = '0'; }
    else {
        while (val > 0) {
            tmp[i++] = digits[val % base];
            val /= base;
        }
    }
    for (int j = i; j < width; j++) *(*buf)++ = pad;
    for (int j = i - 1; j >= 0; j--) *(*buf)++ = tmp[j];
}

static void put_int(char** buf, int32_t val, int width, char pad) {
    if (val < 0) {
        *(*buf)++ = '-';
        val = -val;
        if (width > 0) width--;
    }
    put_uint(buf, (uint32_t)val, 10, width, pad, false);
}

int ksprintf(char* buf, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char* start = buf;

    while (*fmt) {
        if (*fmt != '%') { *buf++ = *fmt++; continue; }
        fmt++;

        char pad = ' ';
        int width = 0;
        if (*fmt == '0') { pad = '0'; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }

        // Handle 'l' prefix for long
        bool is_long = false;
        if (*fmt == 'l') { is_long = true; fmt++; }
        (void)is_long;

        switch (*fmt) {
            case 'd': put_int(&buf, va_arg(ap, int32_t), width, pad); break;
            case 'u': put_uint(&buf, va_arg(ap, uint32_t), 10, width, pad, false); break;
            case 'x': put_uint(&buf, va_arg(ap, uint32_t), 16, width, pad, false); break;
            case 'X': put_uint(&buf, va_arg(ap, uint32_t), 16, width, pad, true); break;
            case 'p': {
                *buf++ = '0'; *buf++ = 'x';
                put_uint(&buf, va_arg(ap, uint32_t), 16, 8, '0', false);
                break;
            }
            case 's': {
                const char* s = va_arg(ap, const char*);
                if (!s) s = "(null)";
                while (*s) *buf++ = *s++;
                break;
            }
            case 'c': *buf++ = (char)va_arg(ap, int); break;
            case '%': *buf++ = '%'; break;
            default: *buf++ = '%'; *buf++ = *fmt; break;
        }
        fmt++;
    }
    *buf = 0;
    va_end(ap);
    return (int)(buf - start);
}

void kprintf(const char* fmt, ...) {
    // Forward to ksprintf then serial
    char buf[256];
    va_list ap;
    va_start(ap, fmt);

    // Inline format since we can't easily forward varargs
    char* p = buf;
    const char* f = fmt;
    while (*f && (p - buf) < 250) {
        if (*f != '%') { *p++ = *f++; continue; }
        f++;
        char pad = ' ';
        int width = 0;
        if (*f == '0') { pad = '0'; f++; }
        while (*f >= '0' && *f <= '9') { width = width * 10 + (*f - '0'); f++; }
        if (*f == 'l') f++;
        switch (*f) {
            case 'd': put_int(&p, va_arg(ap, int32_t), width, pad); break;
            case 'u': put_uint(&p, va_arg(ap, uint32_t), 10, width, pad, false); break;
            case 'x': put_uint(&p, va_arg(ap, uint32_t), 16, width, pad, false); break;
            case 'X': put_uint(&p, va_arg(ap, uint32_t), 16, width, pad, true); break;
            case 'p': *p++ = '0'; *p++ = 'x'; put_uint(&p, va_arg(ap, uint32_t), 16, 8, '0', false); break;
            case 's': { const char* s = va_arg(ap, const char*); if (!s) s = "(null)"; while (*s) *p++ = *s++; break; }
            case 'c': *p++ = (char)va_arg(ap, int); break;
            case '%': *p++ = '%'; break;
            default: *p++ = '%'; *p++ = *f; break;
        }
        f++;
    }
    *p = 0;
    va_end(ap);
    serial_write(buf);
}
