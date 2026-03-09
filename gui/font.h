#pragma once

#include "../lib/types.h"

// Font sizes available
#define FONT_SMALL   0   // 8px height (system/status)
#define FONT_MEDIUM  1   // 12px height (menus, labels)
#define FONT_LARGE   2   // 16px height (titles)

// Styles
#define FONT_REGULAR 0
#define FONT_BOLD    1

void font_init();

// Get character dimensions for a font size
int font_char_width(int size);
int font_char_height(int size);

// Draw single character
void font_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg, int size);
void font_draw_char_nobg(int x, int y, char c, uint32_t fg, int size);

// Draw string
void font_draw_string(int x, int y, const char* str, uint32_t fg, uint32_t bg, int size);
void font_draw_string_nobg(int x, int y, const char* str, uint32_t fg, int size);

// Measure string width in pixels
int font_string_width(const char* str, int size);

// Draw string clipped to a width
void font_draw_string_clipped(int x, int y, const char* str, uint32_t fg, uint32_t bg, int size, int max_width);
