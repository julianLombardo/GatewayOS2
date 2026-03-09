#pragma once

#include "../lib/types.h"

// Screen dimensions (set by VESA mode)
#define SCREEN_WIDTH  1024
#define SCREEN_HEIGHT 768

// NeXTSTEP color constants (ARGB format: 0xAARRGGBB)
#define NX_BLACK    0xFF000000
#define NX_DKGRAY   0xFF555555
#define NX_LTGRAY   0xFFAAAAAA
#define NX_WHITE    0xFFFFFFFF

// Additional UI colors
#define NX_SELECT   0xFF3366CC  // Selection blue
#define NX_RED      0xFFCC3333  // Error/alert red
#define NX_GREEN    0xFF33CC33  // Success green
#define NX_AMBER    0xFFFFBB33  // Gateway amber accent

// Color helpers
#define RGBA(r, g, b, a) ((uint32_t)((a) << 24 | (r) << 16 | (g) << 8 | (b)))
#define RGB(r, g, b)     RGBA(r, g, b, 255)
#define COLOR_R(c) (((c) >> 16) & 0xFF)
#define COLOR_G(c) (((c) >> 8) & 0xFF)
#define COLOR_B(c) ((c) & 0xFF)
#define COLOR_A(c) (((c) >> 24) & 0xFF)

// Framebuffer initialization
void fb_init(uint32_t* addr, int width, int height, int pitch);

// Basic drawing
void fb_clear(uint32_t color);
void fb_putpixel(int x, int y, uint32_t color);
uint32_t fb_getpixel(int x, int y);
void fb_fillrect(int x, int y, int w, int h, uint32_t color);
void fb_rect(int x, int y, int w, int h, uint32_t color);
void fb_hline(int x, int y, int w, uint32_t color);
void fb_vline(int x, int y, int h, uint32_t color);

// Alpha blending
void fb_putpixel_alpha(int x, int y, uint32_t color);
void fb_fillrect_alpha(int x, int y, int w, int h, uint32_t color);

// Bitmap blitting
void fb_blit(int dx, int dy, int w, int h, const uint32_t* src, int src_pitch);
void fb_blit_alpha(int dx, int dy, int w, int h, const uint32_t* src, int src_pitch);

// Double buffering
void fb_flip();
void fb_flip_rect(int x, int y, int w, int h);

// Clipping
void fb_set_clip(int x, int y, int w, int h);
void fb_reset_clip();

// Screen info
int fb_get_width();
int fb_get_height();
uint32_t* fb_get_backbuffer();
