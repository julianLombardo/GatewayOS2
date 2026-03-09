#include "desktop.h"
#include "window.h"
#include "dock.h"
#include "menu.h"
#include "theme.h"
#include "font.h"
#include "../drivers/framebuffer.h"
#include "../drivers/keyboard.h"
#include "../lib/string.h"

// Drag state
static bool dragging = false;
static bool resizing = false;
static int drag_win_id = -1;
static int drag_offset_x = 0, drag_offset_y = 0;
static int resize_start_w = 0, resize_start_h = 0;
static int resize_start_mx = 0, resize_start_my = 0;
static bool shutdown_flag = false;

// Mouse cursor
static int cursor_prev_x = -1, cursor_prev_y = -1;

void desktop_init() {
    dragging = false;
    resizing = false;
    drag_win_id = -1;
    shutdown_flag = false;
}

// Draw gradient desktop wallpaper
static void desktop_draw_wallpaper() {
    int dock_x = SCREEN_WIDTH - DOCK_TILE_SIZE - 1;
    for (int y = MENUBAR_HEIGHT; y < SCREEN_HEIGHT; y++) {
        // Vertical gradient: dark blue-gray at top -> darker at bottom
        int t = ((y - MENUBAR_HEIGHT) * 256) / (SCREEN_HEIGHT - MENUBAR_HEIGHT);
        uint8_t r = 45 - (t * 20 / 256);
        uint8_t g = 55 - (t * 25 / 256);
        uint8_t b = 80 - (t * 30 / 256);
        uint32_t col = RGB(r, g, b);
        fb_hline(0, y, dock_x, col);
    }

    // Subtle grid pattern overlay
    for (int y = MENUBAR_HEIGHT + 16; y < SCREEN_HEIGHT; y += 32) {
        for (int x = 16; x < dock_x; x += 32) {
            fb_putpixel(x, y, RGB(60, 70, 95));
        }
    }

    // "GATEWAY" watermark in bottom-left corner
    int wy = SCREEN_HEIGHT - 28;
    font_draw_string_nobg(12, wy, "GATEWAY OS2", RGB(40, 48, 70), FONT_MEDIUM);
    font_draw_string_nobg(12, wy + 14, "v1.0", RGB(35, 42, 62), FONT_SMALL);
}

void desktop_draw() {
    // Desktop wallpaper (gradient background)
    desktop_draw_wallpaper();

    // Draw all windows
    wm_draw_all();

    // Draw dock (on top of everything except menus)
    dock_draw();

    // Draw menubar (horizontal bar at top)
    menubar_draw();

    // Draw dropdown menus (topmost)
    menu_draw_all();

    // Draw mouse cursor — NeXT-style arrow
    int mx = cursor_prev_x;
    int my = cursor_prev_y;
    if (mx >= 0 && my >= 0) {
        // Cursor bitmap: 12x16 arrow
        // 1=black outline, 2=white fill
        static const uint8_t cursor[16][12] = {
            {1,0,0,0,0,0,0,0,0,0,0,0},
            {1,1,0,0,0,0,0,0,0,0,0,0},
            {1,2,1,0,0,0,0,0,0,0,0,0},
            {1,2,2,1,0,0,0,0,0,0,0,0},
            {1,2,2,2,1,0,0,0,0,0,0,0},
            {1,2,2,2,2,1,0,0,0,0,0,0},
            {1,2,2,2,2,2,1,0,0,0,0,0},
            {1,2,2,2,2,2,2,1,0,0,0,0},
            {1,2,2,2,2,2,2,2,1,0,0,0},
            {1,2,2,2,2,2,2,2,2,1,0,0},
            {1,2,2,2,2,2,1,1,1,1,1,0},
            {1,2,2,1,2,2,1,0,0,0,0,0},
            {1,2,1,0,1,2,2,1,0,0,0,0},
            {1,1,0,0,1,2,2,1,0,0,0,0},
            {1,0,0,0,0,1,2,2,1,0,0,0},
            {0,0,0,0,0,1,1,1,0,0,0,0},
        };
        for (int cy2 = 0; cy2 < 16; cy2++) {
            for (int cx2 = 0; cx2 < 12; cx2++) {
                if (cursor[cy2][cx2] == 1)
                    fb_putpixel(mx + cx2, my + cy2, NX_BLACK);
                else if (cursor[cy2][cx2] == 2)
                    fb_putpixel(mx + cx2, my + cy2, NX_WHITE);
            }
        }
    }

    fb_flip();
}

void desktop_handle_mouse(int mx, int my, bool left, bool right) {
    cursor_prev_x = mx;
    cursor_prev_y = my;

    // Right-click: show context menu
    if (right) {
        // Close any open menubar dropdown
        menu_hide_all();
        // Show context menu at cursor
        menu_show_context(mx, my);
        return;
    }

    if (!left) return;

    // Check menubar first (horizontal bar at top)
    if (menubar_handle_mouse(mx, my, true))
        return;

    // Check open dropdown/context menus
    if (menu_handle_mouse(mx, my, true))
        return;

    // Close any open menus when clicking elsewhere
    if (menu_is_active())
        menu_hide_all();

    // Check dock — launch the app
    int dock_idx = dock_hit_test(mx, my);
    if (dock_idx >= 0) {
        dock_launch(dock_idx);
        return;
    }

    // Check windows (top to bottom z-order)
    Window* win = wm_window_at(mx, my);
    if (win) {
        int hit = wm_hit_test(win, mx, my);
        wm_set_focus(win->id);

        switch (hit) {
            case HIT_CLOSE:
                wm_close_window(win->id);
                break;
            case HIT_MINIATURIZE:
                wm_minimize_window(win->id);
                break;
            case HIT_TITLE_BAR:
                dragging = true;
                drag_win_id = win->id;
                drag_offset_x = mx - win->x;
                drag_offset_y = my - win->y;
                break;
            case HIT_RESIZE:
                resizing = true;
                drag_win_id = win->id;
                resize_start_w = win->w;
                resize_start_h = win->h;
                resize_start_mx = mx;
                resize_start_my = my;
                break;
            case HIT_CONTENT:
                if (win->on_mouse)
                    win->on_mouse(win, mx, my, true, false);
                break;
        }
    }
}

void desktop_handle_mouse_move(int mx, int my, bool left_held) {
    cursor_prev_x = mx;
    cursor_prev_y = my;

    // Update menubar hover (switch menus while dragging over bar)
    if (my < MENUBAR_HEIGHT) {
        menubar_handle_mouse(mx, my, false);
    }
    // Update dropdown menu hover
    if (menu_is_active()) {
        menu_handle_mouse(mx, my, false);
    }

    if (!left_held) {
        dragging = false;
        resizing = false;
        drag_win_id = -1;
        return;
    }

    if (dragging && drag_win_id >= 0) {
        int new_x = mx - drag_offset_x;
        int new_y = my - drag_offset_y;
        if (new_y < MENUBAR_HEIGHT) new_y = MENUBAR_HEIGHT;
        wm_move_window(drag_win_id, new_x, new_y);
    }

    if (resizing && drag_win_id >= 0) {
        int dw = mx - resize_start_mx;
        int dh = my - resize_start_my;
        wm_resize_window(drag_win_id, resize_start_w + dw, resize_start_h + dh);
    }
}

void desktop_handle_key(uint8_t key) {
    // Alt+Tab: cycle windows
    if (keyboard_alt_held() && key == KEY_TAB) {
        // Find next visible window
        Window* focused = wm_get_focused();
        int start = focused ? focused->id + 1 : 0;
        for (int i = 0; i < wm_get_count(); i++) {
            int idx = (start + i) % wm_get_count();
            Window* w = wm_get_by_index(idx);
            if (w && (w->flags & WIN_VISIBLE) && !(w->flags & WIN_MINIMIZED)) {
                wm_set_focus(w->id);
                break;
            }
        }
        return;
    }

    // Alt+F4: close focused window
    if (keyboard_alt_held() && key == KEY_F4) {
        Window* focused = wm_get_focused();
        if (focused && (focused->flags & WIN_CLOSEABLE))
            wm_close_window(focused->id);
        return;
    }

    // Send key to focused window
    Window* focused = wm_get_focused();
    if (focused && focused->on_key) {
        focused->on_key(focused, key);
    }
}

void desktop_request_shutdown() {
    shutdown_flag = true;
}

void desktop_request_hide() {
    // Hide focused window
    Window* w = wm_get_focused();
    if (w) wm_minimize_window(w->id);
}

bool desktop_shutdown_requested() {
    return shutdown_flag;
}
