#include "window.h"
#include "theme.h"
#include "font.h"
#include "../drivers/framebuffer.h"
#include "../lib/string.h"
#include "../lib/printf.h"

static Window windows[MAX_WINDOWS];
static int window_count = 0;
static int focused_id = -1;

// Z-order: index 0 = bottom, window_count-1 = top
static int z_order[MAX_WINDOWS];
static int z_count = 0;

void wm_init() {
    memset(windows, 0, sizeof(windows));
    window_count = 0;
    focused_id = -1;
    z_count = 0;
}

Window* wm_create_window(const char* title, int x, int y, int w, int h, uint32_t flags) {
    if (window_count >= MAX_WINDOWS) return NULL;

    Window* win = &windows[window_count];
    memset(win, 0, sizeof(Window));
    win->id = window_count;
    strncpy(win->title, title, 63);
    win->x = x; win->y = y;
    win->w = w; win->h = h;
    win->flags = flags | WIN_VISIBLE;

    z_order[z_count++] = win->id;
    window_count++;

    wm_set_focus(win->id);
    return win;
}

void wm_close_window(int id) {
    Window* win = wm_get_window(id);
    if (!win) return;

    if (win->on_close) win->on_close(win);

    win->flags = 0; // Mark as dead
    win->title[0] = 0;

    // Remove from z-order
    for (int i = 0; i < z_count; i++) {
        if (z_order[i] == id) {
            for (int j = i; j < z_count - 1; j++)
                z_order[j] = z_order[j + 1];
            z_count--;
            break;
        }
    }

    // Focus next visible window
    if (focused_id == id) {
        focused_id = -1;
        for (int i = z_count - 1; i >= 0; i--) {
            Window* w = wm_get_window(z_order[i]);
            if (w && (w->flags & WIN_VISIBLE) && !(w->flags & WIN_MINIMIZED)) {
                focused_id = w->id;
                break;
            }
        }
    }
}

void wm_minimize_window(int id) {
    Window* win = wm_get_window(id);
    if (win) win->flags |= WIN_MINIMIZED;

    if (focused_id == id) {
        focused_id = -1;
        for (int i = z_count - 1; i >= 0; i--) {
            Window* w = wm_get_window(z_order[i]);
            if (w && (w->flags & WIN_VISIBLE) && !(w->flags & WIN_MINIMIZED) && w->id != id) {
                focused_id = w->id;
                break;
            }
        }
    }
}

void wm_restore_window(int id) {
    Window* win = wm_get_window(id);
    if (win) {
        win->flags &= ~WIN_MINIMIZED;
        wm_raise_window(id);
        wm_set_focus(id);
    }
}

Window* wm_get_window(int id) {
    if (id < 0 || id >= window_count) return NULL;
    if (!(windows[id].flags & WIN_VISIBLE) && windows[id].title[0] == 0) return NULL;
    return &windows[id];
}

Window* wm_get_focused() {
    return wm_get_window(focused_id);
}

void wm_set_focus(int id) {
    focused_id = id;
    wm_raise_window(id);
}

void wm_raise_window(int id) {
    // Move to top of z-order
    int pos = -1;
    for (int i = 0; i < z_count; i++) {
        if (z_order[i] == id) { pos = i; break; }
    }
    if (pos < 0) return;
    for (int i = pos; i < z_count - 1; i++)
        z_order[i] = z_order[i + 1];
    z_order[z_count - 1] = id;
}

void wm_move_window(int id, int x, int y) {
    Window* win = wm_get_window(id);
    if (win) { win->x = x; win->y = y; }
}

void wm_resize_window(int id, int w, int h) {
    Window* win = wm_get_window(id);
    if (!win) return;
    if (w < 100) w = 100;
    if (h < 60) h = 60;
    win->w = w; win->h = h;
    if (win->on_resize) win->on_resize(win, w, h);
}

int wm_get_count() { return window_count; }

Window* wm_get_by_index(int index) {
    if (index < 0 || index >= window_count) return NULL;
    return &windows[index];
}

int wm_get_visible_count() {
    int count = 0;
    for (int i = 0; i < window_count; i++) {
        if ((windows[i].flags & WIN_VISIBLE) && !(windows[i].flags & WIN_MINIMIZED))
            count++;
    }
    return count;
}

void wm_get_content_area(Window* win, int* cx, int* cy, int* cw, int* ch) {
    if (!win) return;
    int border = 3; // 1px black + 1px bevel + 1px padding
    int sb_w = (win->flags & WIN_HAS_SCROLL) ? SCROLLBAR_WIDTH : 0;
    *cx = win->x + border;
    *cy = win->y + TITLE_BAR_HEIGHT + 1;
    *cw = win->w - border * 2 - sb_w;
    *ch = win->h - TITLE_BAR_HEIGHT - 1 - border;
}

void wm_draw_window(Window* win) {
    if (!win || !(win->flags & WIN_VISIBLE) || (win->flags & WIN_MINIMIZED)) return;

    bool focused = (win->id == focused_id);

    // Drop shadow
    nx_draw_shadow(win->x, win->y, win->w, win->h, 4);

    // Window background (the entire window including title bar)
    fb_fillrect(win->x, win->y, win->w, win->h, NX_LTGRAY);

    // Outer border — blue accent for focused, black for unfocused
    uint32_t border_col = focused ? RGB(60, 100, 180) : NX_BLACK;
    fb_rect(win->x, win->y, win->w, win->h, border_col);
    if (focused) {
        // Double-width accent border for focused window
        fb_rect(win->x - 1, win->y - 1, win->w + 2, win->h + 2, border_col);
    }

    // 3D bevel on window frame
    fb_hline(win->x + 1, win->y + 1, win->w - 2, NX_WHITE);
    fb_vline(win->x + 1, win->y + 1, win->h - 2, NX_WHITE);
    fb_hline(win->x + 1, win->y + win->h - 2, win->w - 2, NX_DKGRAY);
    fb_vline(win->x + win->w - 2, win->y + 1, win->h - 2, NX_DKGRAY);

    // Title bar
    int tb_x = win->x + 2;
    int tb_y = win->y + 2;
    int tb_w = win->w - 4;
    int tb_h = TITLE_BAR_HEIGHT - 3;

    if (focused) {
        nx_draw_raised_color(tb_x, tb_y, tb_w, tb_h, NX_LTGRAY);
    } else {
        nx_draw_raised_color(tb_x, tb_y, tb_w, tb_h, NX_DKGRAY);
    }

    // Title text (centered)
    uint32_t title_fg = focused ? NX_BLACK : NX_WHITE;
    int tw = font_string_width(win->title, FONT_MEDIUM);
    int tx = tb_x + (tb_w - tw) / 2;
    int ty = tb_y + (tb_h - font_char_height(FONT_MEDIUM)) / 2;
    font_draw_string_nobg(tx, ty, win->title, title_fg, FONT_MEDIUM);

    // Close button (left side of title bar)
    if (win->flags & WIN_CLOSEABLE) {
        int bx = tb_x + 4;
        int by = tb_y + (tb_h - 15) / 2;
        nx_draw_raised(bx, by, 15, 15);
        // X mark
        for (int i = 0; i < 7; i++) {
            fb_putpixel(bx + 4 + i, by + 4 + i, NX_BLACK);
            fb_putpixel(bx + 10 - i, by + 4 + i, NX_BLACK);
        }
    }

    // Miniaturize button (right side of title bar)
    if (win->flags & WIN_MINIATURIZE) {
        int bx = tb_x + tb_w - 19;
        int by = tb_y + (tb_h - 15) / 2;
        nx_draw_raised(bx, by, 15, 15);
        // Small window icon
        fb_rect(bx + 4, by + 6, 7, 5, NX_BLACK);
        fb_hline(bx + 4, by + 7, 7, NX_BLACK);
    }

    // Separator between title bar and content
    fb_hline(win->x + 1, win->y + TITLE_BAR_HEIGHT, win->w - 2, NX_BLACK);

    // Content area background
    int cx, cy, cw, ch;
    wm_get_content_area(win, &cx, &cy, &cw, &ch);
    fb_fillrect(cx, cy, cw, ch, NX_LTGRAY);

    // Scrollbar
    if (win->flags & WIN_HAS_SCROLL) {
        int sb_x = win->x + win->w - SCROLLBAR_WIDTH - 3;
        int sb_y = win->y + TITLE_BAR_HEIGHT + 1;
        int sb_h = win->h - TITLE_BAR_HEIGHT - 4;
        nx_draw_vscrollbar(sb_x, sb_y, sb_h,
                          win->content_h, win->scroll_y,
                          ch);
    }

    // Resize handle
    if (win->flags & WIN_RESIZABLE) {
        int rx = win->x + win->w - RESIZE_HANDLE_SIZE - 2;
        int ry = win->y + win->h - RESIZE_HANDLE_SIZE - 2;
        nx_draw_resize_handle(rx, ry);
    }

    // Draw content via callback
    if (win->on_draw) {
        fb_set_clip(cx, cy, cw, ch);
        win->on_draw(win, cx, cy, cw, ch);
        fb_reset_clip();
    }
}

void wm_draw_all() {
    // Draw in z-order (bottom to top)
    for (int i = 0; i < z_count; i++) {
        wm_draw_window(wm_get_window(z_order[i]));
    }
}

int wm_hit_test(Window* win, int mx, int my) {
    if (!win || !(win->flags & WIN_VISIBLE) || (win->flags & WIN_MINIMIZED)) return HIT_NONE;

    // Outside window?
    if (mx < win->x || mx >= win->x + win->w ||
        my < win->y || my >= win->y + win->h)
        return HIT_NONE;

    // Title bar area
    if (my < win->y + TITLE_BAR_HEIGHT) {
        int tb_x = win->x + 2;
        int tb_w = win->w - 4;
        int tb_h = TITLE_BAR_HEIGHT - 3;

        // Close button
        if ((win->flags & WIN_CLOSEABLE) && mx >= tb_x + 4 && mx < tb_x + 19 &&
            my >= win->y + 2 + (tb_h - 15) / 2 && my < win->y + 2 + (tb_h - 15) / 2 + 15)
            return HIT_CLOSE;

        // Miniaturize button
        if ((win->flags & WIN_MINIATURIZE) && mx >= tb_x + tb_w - 19 && mx < tb_x + tb_w - 4 &&
            my >= win->y + 2 + (tb_h - 15) / 2 && my < win->y + 2 + (tb_h - 15) / 2 + 15)
            return HIT_MINIATURIZE;

        return HIT_TITLE_BAR;
    }

    // Resize handle
    if ((win->flags & WIN_RESIZABLE) &&
        mx >= win->x + win->w - RESIZE_HANDLE_SIZE - 2 &&
        my >= win->y + win->h - RESIZE_HANDLE_SIZE - 2)
        return HIT_RESIZE;

    // Scrollbar
    if ((win->flags & WIN_HAS_SCROLL) &&
        mx >= win->x + win->w - SCROLLBAR_WIDTH - 3)
        return HIT_SCROLLBAR;

    return HIT_CONTENT;
}

Window* wm_window_at(int mx, int my) {
    // Search top-to-bottom in z-order
    for (int i = z_count - 1; i >= 0; i--) {
        Window* win = wm_get_window(z_order[i]);
        if (!win || !(win->flags & WIN_VISIBLE) || (win->flags & WIN_MINIMIZED)) continue;
        if (mx >= win->x && mx < win->x + win->w &&
            my >= win->y && my < win->y + win->h)
            return win;
    }
    return NULL;
}
