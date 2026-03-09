#include "framebuffer.h"
#include "../lib/string.h"

static uint32_t* fb_front = NULL;    // Hardware framebuffer (VESA LFB)
static uint32_t  fb_back_buf[SCREEN_WIDTH * SCREEN_HEIGHT]; // Back buffer
static uint32_t* fb_back = fb_back_buf;
static int fb_width = SCREEN_WIDTH;
static int fb_height = SCREEN_HEIGHT;
static int fb_pitch = SCREEN_WIDTH;  // In pixels (not bytes)

// Clipping rectangle
static int clip_x = 0, clip_y = 0;
static int clip_w = SCREEN_WIDTH, clip_h = SCREEN_HEIGHT;

void fb_init(uint32_t* addr, int width, int height, int pitch) {
    fb_front = addr;
    fb_width = width;
    fb_height = height;
    fb_pitch = pitch / 4; // Convert byte pitch to pixel pitch
    clip_x = 0; clip_y = 0;
    clip_w = width; clip_h = height;
}

void fb_clear(uint32_t color) {
    for (int i = 0; i < fb_width * fb_height; i++)
        fb_back[i] = color;
}

static inline bool in_clip(int x, int y) {
    return x >= clip_x && x < clip_x + clip_w &&
           y >= clip_y && y < clip_y + clip_h;
}

void fb_putpixel(int x, int y, uint32_t color) {
    if (x >= 0 && x < fb_width && y >= 0 && y < fb_height && in_clip(x, y))
        fb_back[y * fb_width + x] = color;
}

uint32_t fb_getpixel(int x, int y) {
    if (x >= 0 && x < fb_width && y >= 0 && y < fb_height)
        return fb_back[y * fb_width + x];
    return 0;
}

void fb_fillrect(int x, int y, int w, int h, uint32_t color) {
    // Clip to screen and clip rect
    int x0 = x < clip_x ? clip_x : x;
    int y0 = y < clip_y ? clip_y : y;
    int x1 = x + w; if (x1 > clip_x + clip_w) x1 = clip_x + clip_w;
    int y1 = y + h; if (y1 > clip_y + clip_h) y1 = clip_y + clip_h;
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 > fb_width) x1 = fb_width;
    if (y1 > fb_height) y1 = fb_height;

    for (int j = y0; j < y1; j++) {
        uint32_t* row = fb_back + j * fb_width;
        for (int i = x0; i < x1; i++)
            row[i] = color;
    }
}

void fb_rect(int x, int y, int w, int h, uint32_t color) {
    fb_hline(x, y, w, color);
    fb_hline(x, y + h - 1, w, color);
    fb_vline(x, y, h, color);
    fb_vline(x + w - 1, y, h, color);
}

void fb_hline(int x, int y, int w, uint32_t color) {
    if (y < 0 || y >= fb_height || y < clip_y || y >= clip_y + clip_h) return;
    int x0 = x < clip_x ? clip_x : x;
    int x1 = x + w; if (x1 > clip_x + clip_w) x1 = clip_x + clip_w;
    if (x0 < 0) x0 = 0; if (x1 > fb_width) x1 = fb_width;
    uint32_t* row = fb_back + y * fb_width;
    for (int i = x0; i < x1; i++) row[i] = color;
}

void fb_vline(int x, int y, int h, uint32_t color) {
    if (x < 0 || x >= fb_width || x < clip_x || x >= clip_x + clip_w) return;
    int y0 = y < clip_y ? clip_y : y;
    int y1 = y + h; if (y1 > clip_y + clip_h) y1 = clip_y + clip_h;
    if (y0 < 0) y0 = 0; if (y1 > fb_height) y1 = fb_height;
    for (int j = y0; j < y1; j++)
        fb_back[j * fb_width + x] = color;
}

// Alpha blend: src over dst
static inline uint32_t blend(uint32_t src, uint32_t dst) {
    uint32_t sa = COLOR_A(src);
    if (sa == 255) return src;
    if (sa == 0) return dst;
    uint32_t da = 255 - sa;
    uint32_t r = (COLOR_R(src) * sa + COLOR_R(dst) * da) / 255;
    uint32_t g = (COLOR_G(src) * sa + COLOR_G(dst) * da) / 255;
    uint32_t b = (COLOR_B(src) * sa + COLOR_B(dst) * da) / 255;
    return RGB(r, g, b);
}

void fb_putpixel_alpha(int x, int y, uint32_t color) {
    if (x >= 0 && x < fb_width && y >= 0 && y < fb_height && in_clip(x, y)) {
        int idx = y * fb_width + x;
        fb_back[idx] = blend(color, fb_back[idx]);
    }
}

void fb_fillrect_alpha(int x, int y, int w, int h, uint32_t color) {
    int x0 = x < clip_x ? clip_x : x;
    int y0 = y < clip_y ? clip_y : y;
    int x1 = x + w; if (x1 > clip_x + clip_w) x1 = clip_x + clip_w;
    int y1 = y + h; if (y1 > clip_y + clip_h) y1 = clip_y + clip_h;
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 > fb_width) x1 = fb_width;
    if (y1 > fb_height) y1 = fb_height;

    for (int j = y0; j < y1; j++) {
        uint32_t* row = fb_back + j * fb_width;
        for (int i = x0; i < x1; i++)
            row[i] = blend(color, row[i]);
    }
}

void fb_blit(int dx, int dy, int w, int h, const uint32_t* src, int src_pitch) {
    for (int j = 0; j < h; j++) {
        int sy = dy + j;
        if (sy < 0 || sy >= fb_height) continue;
        for (int i = 0; i < w; i++) {
            int sx = dx + i;
            if (sx < 0 || sx >= fb_width) continue;
            if (in_clip(sx, sy))
                fb_back[sy * fb_width + sx] = src[j * src_pitch + i];
        }
    }
}

void fb_blit_alpha(int dx, int dy, int w, int h, const uint32_t* src, int src_pitch) {
    for (int j = 0; j < h; j++) {
        int sy = dy + j;
        if (sy < 0 || sy >= fb_height) continue;
        for (int i = 0; i < w; i++) {
            int sx = dx + i;
            if (sx < 0 || sx >= fb_width) continue;
            if (in_clip(sx, sy)) {
                int idx = sy * fb_width + sx;
                fb_back[idx] = blend(src[j * src_pitch + i], fb_back[idx]);
            }
        }
    }
}

void fb_flip() {
    if (fb_pitch == fb_width) {
        memcpy(fb_front, fb_back, fb_width * fb_height * 4);
    } else {
        for (int j = 0; j < fb_height; j++)
            memcpy(fb_front + j * fb_pitch, fb_back + j * fb_width, fb_width * 4);
    }
}

void fb_flip_rect(int x, int y, int w, int h) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > fb_width) w = fb_width - x;
    if (y + h > fb_height) h = fb_height - y;
    for (int j = y; j < y + h; j++) {
        memcpy(fb_front + j * fb_pitch + x,
               fb_back + j * fb_width + x,
               w * 4);
    }
}

void fb_set_clip(int x, int y, int w, int h) {
    clip_x = x; clip_y = y; clip_w = w; clip_h = h;
}

void fb_reset_clip() {
    clip_x = 0; clip_y = 0; clip_w = fb_width; clip_h = fb_height;
}

int fb_get_width() { return fb_width; }
int fb_get_height() { return fb_height; }
uint32_t* fb_get_backbuffer() { return fb_back; }
