#pragma once

#include "../lib/types.h"

#define VGA_BLACK   0
#define VGA_BLUE    1
#define VGA_GREEN   2
#define VGA_CYAN    3
#define VGA_RED     4
#define VGA_MAGENTA 5
#define VGA_BROWN   6
#define VGA_LGRAY   7
#define VGA_DGRAY   8
#define VGA_LBLUE   9
#define VGA_LGREEN  10
#define VGA_LCYAN   11
#define VGA_LRED    12
#define VGA_LMAG    13
#define VGA_YELLOW  14
#define VGA_WHITE   15

void terminal_init();
void terminal_clear();
void terminal_setcolor(uint8_t fg, uint8_t bg);
void terminal_write(const char* str);
void terminal_set_cursor(int x, int y);
