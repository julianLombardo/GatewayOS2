#pragma once

#include "../lib/types.h"

#define MAX_WINDOWS 32
#define TITLE_BAR_HEIGHT 23
#define RESIZE_HANDLE_SIZE 12
#define SCROLLBAR_WIDTH 18

// Window flags
#define WIN_VISIBLE     0x0001
#define WIN_CLOSEABLE   0x0002
#define WIN_MINIATURIZE 0x0004
#define WIN_RESIZABLE   0x0008
#define WIN_HAS_SCROLL  0x0010
#define WIN_MINIMIZED   0x0020
#define WIN_PANEL       0x0040  // Utility/floating panel
#define WIN_MENU        0x0080  // Menu window (no title bar chrome)

struct Window {
    int id;
    char title[64];
    int x, y, w, h;        // Total area including chrome
    uint32_t flags;

    // Scroll state
    int scroll_x, scroll_y;
    int content_w, content_h;

    // Callbacks
    void (*on_draw)(Window* win, int cx, int cy, int cw, int ch);
    void (*on_key)(Window* win, uint8_t key);
    void (*on_mouse)(Window* win, int mx, int my, bool left, bool right);
    void (*on_close)(Window* win);
    void (*on_resize)(Window* win, int new_w, int new_h);

    void* userdata;
};

// Window manager
void wm_init();
Window* wm_create_window(const char* title, int x, int y, int w, int h, uint32_t flags);
void wm_close_window(int id);
void wm_minimize_window(int id);
void wm_restore_window(int id);
Window* wm_get_window(int id);
Window* wm_get_focused();
void wm_set_focus(int id);
void wm_raise_window(int id);
void wm_move_window(int id, int x, int y);
void wm_resize_window(int id, int w, int h);

// Iteration
int wm_get_count();
Window* wm_get_by_index(int index);
int wm_get_visible_count();
static inline int wm_window_count() { return wm_get_count(); }

// Get content area (excluding title bar and borders)
void wm_get_content_area(Window* win, int* cx, int* cy, int* cw, int* ch);

// Drawing
void wm_draw_window(Window* win);
void wm_draw_all();

// Hit testing
#define HIT_NONE        0
#define HIT_TITLE_BAR   1
#define HIT_CLOSE       2
#define HIT_MINIATURIZE 3
#define HIT_RESIZE      4
#define HIT_CONTENT     5
#define HIT_SCROLLBAR   6
int wm_hit_test(Window* win, int mx, int my);
Window* wm_window_at(int mx, int my);
