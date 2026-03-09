#include "clipboard.h"
#include "../lib/string.h"

static char clip_buf[CLIPBOARD_MAX];
static int  clip_len = 0;

void clipboard_init() {
    clip_buf[0] = 0;
    clip_len = 0;
}

void clipboard_copy(const char* text, int len) {
    if (len <= 0) return;
    if (len > CLIPBOARD_MAX - 1) len = CLIPBOARD_MAX - 1;
    memcpy(clip_buf, text, len);
    clip_buf[len] = 0;
    clip_len = len;
}

const char* clipboard_paste(int* out_len) {
    if (out_len) *out_len = clip_len;
    return clip_buf;
}

void clipboard_clear() {
    clip_buf[0] = 0;
    clip_len = 0;
}
