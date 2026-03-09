#include "vga_text.h"
#include "../lib/string.h"

static uint16_t* const VGA_MEMORY = (uint16_t*)0xB8000;
static int cursor_x = 0, cursor_y = 0;
static uint8_t color = 0x0F;

void terminal_init() {
    cursor_x = 0;
    cursor_y = 0;
    color = (VGA_BLACK << 4) | VGA_LGRAY;
    terminal_clear();
}

void terminal_clear() {
    for (int i = 0; i < 80 * 25; i++)
        VGA_MEMORY[i] = (uint16_t)(' ' | (color << 8));
    cursor_x = 0;
    cursor_y = 0;
}

void terminal_setcolor(uint8_t fg, uint8_t bg) {
    color = (bg << 4) | fg;
}

void terminal_set_cursor(int x, int y) {
    cursor_x = x;
    cursor_y = y;
}

static void scroll() {
    for (int i = 0; i < 80 * 24; i++)
        VGA_MEMORY[i] = VGA_MEMORY[i + 80];
    for (int i = 80 * 24; i < 80 * 25; i++)
        VGA_MEMORY[i] = (uint16_t)(' ' | (color << 8));
    cursor_y = 24;
}

void terminal_write(const char* str) {
    while (*str) {
        if (*str == '\n') {
            cursor_x = 0;
            cursor_y++;
        } else {
            VGA_MEMORY[cursor_y * 80 + cursor_x] = (uint16_t)(*str | (color << 8));
            cursor_x++;
            if (cursor_x >= 80) { cursor_x = 0; cursor_y++; }
        }
        if (cursor_y >= 25) scroll();
        str++;
    }
}
