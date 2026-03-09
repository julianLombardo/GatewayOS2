#include "apps.h"
#include "../memory/heap.h"

#define EDIT_MAX_LINES 200
#define EDIT_LINE_LEN 120

struct EditState {
    char lines[EDIT_MAX_LINES][EDIT_LINE_LEN + 1];
    int line_count;
    int cursor_x, cursor_y; // Cursor position (col, line)
    int scroll_y;           // First visible line
    int scroll_x;           // Horizontal scroll
    bool modified;
};

static int edit_visible_cols(int cw) {
    return (cw - 50) / font_char_width(FONT_SMALL); // 50px for line numbers
}
static int edit_visible_rows(int ch) {
    return (ch - 4) / 10;
}

static void edit_insert_char(EditState* e, char c) {
    int len = strlen(e->lines[e->cursor_y]);
    if (len >= EDIT_LINE_LEN) return;
    // Shift right from cursor
    for (int i = len + 1; i > e->cursor_x; i--)
        e->lines[e->cursor_y][i] = e->lines[e->cursor_y][i - 1];
    e->lines[e->cursor_y][e->cursor_x] = c;
    e->cursor_x++;
    e->modified = true;
}

static void edit_newline(EditState* e) {
    if (e->line_count >= EDIT_MAX_LINES) return;
    // Shift lines down
    for (int i = e->line_count; i > e->cursor_y + 1; i--)
        memcpy(e->lines[i], e->lines[i - 1], EDIT_LINE_LEN + 1);
    e->line_count++;
    // Split current line at cursor
    char* cur = e->lines[e->cursor_y];
    char* next = e->lines[e->cursor_y + 1];
    strcpy(next, cur + e->cursor_x);
    cur[e->cursor_x] = 0;
    e->cursor_y++;
    e->cursor_x = 0;
    e->modified = true;
}

static void edit_backspace(EditState* e) {
    if (e->cursor_x > 0) {
        char* line = e->lines[e->cursor_y];
        int len = strlen(line);
        for (int i = e->cursor_x - 1; i < len; i++)
            line[i] = line[i + 1];
        e->cursor_x--;
        e->modified = true;
    } else if (e->cursor_y > 0) {
        // Merge with previous line
        int prev_len = strlen(e->lines[e->cursor_y - 1]);
        strcat(e->lines[e->cursor_y - 1], e->lines[e->cursor_y]);
        // Shift lines up
        for (int i = e->cursor_y; i < e->line_count - 1; i++)
            memcpy(e->lines[i], e->lines[i + 1], EDIT_LINE_LEN + 1);
        e->line_count--;
        e->cursor_y--;
        e->cursor_x = prev_len;
        e->modified = true;
    }
}

static void edit_ensure_visible(EditState* e, int ch) {
    int vis = edit_visible_rows(ch);
    if (e->cursor_y < e->scroll_y)
        e->scroll_y = e->cursor_y;
    if (e->cursor_y >= e->scroll_y + vis)
        e->scroll_y = e->cursor_y - vis + 1;
}

static void edit_draw(Window* win, int cx, int cy, int cw, int ch) {
    EditState* e = (EditState*)win->userdata;
    if (!e) return;

    fb_fillrect(cx, cy, cw, ch, NX_WHITE);

    int vis_rows = edit_visible_rows(ch);
    int vis_cols = edit_visible_cols(cw);
    int line_num_w = 40;

    // Line number gutter
    fb_fillrect(cx, cy, line_num_w, ch, RGB(230, 230, 230));
    fb_vline(cx + line_num_w, cy, ch, NX_LTGRAY);

    for (int i = 0; i < vis_rows && (e->scroll_y + i) < e->line_count; i++) {
        int line_idx = e->scroll_y + i;
        int y = cy + 2 + i * 10;

        // Line number
        char num[8];
        ksprintf(num, "%3d", line_idx + 1);
        font_draw_string(cx + 4, y, num, NX_DKGRAY, RGB(230, 230, 230), FONT_SMALL);

        // Line text
        char* line = e->lines[line_idx];
        int len = strlen(line);
        int start = e->scroll_x;
        if (start < len) {
            char visible[EDIT_LINE_LEN + 1];
            int copy_len = len - start;
            if (copy_len > vis_cols) copy_len = vis_cols;
            memcpy(visible, line + start, copy_len);
            visible[copy_len] = 0;
            font_draw_string(cx + line_num_w + 4, y, visible, NX_BLACK, NX_WHITE, FONT_SMALL);
        }

        // Cursor
        if (line_idx == e->cursor_y && (timer_get_ticks() / 50) % 2 == 0) {
            int cur_screen_x = cx + line_num_w + 4 + (e->cursor_x - e->scroll_x) * font_char_width(FONT_SMALL);
            fb_fillrect(cur_screen_x, y, 1, 9, NX_BLACK);
        }
    }

    // Status bar at bottom
    fb_fillrect(cx, cy + ch - 14, cw, 14, NX_LTGRAY);
    nx_draw_separator(cx, cy + ch - 14, cw);
    char status[64];
    ksprintf(status, " Ln %d, Col %d  Lines: %d  %s",
            e->cursor_y + 1, e->cursor_x + 1, e->line_count,
            e->modified ? "[Modified]" : "");
    font_draw_string(cx + 4, cy + ch - 11, status, NX_BLACK, NX_LTGRAY, FONT_SMALL);
}

static void edit_key(Window* win, uint8_t key) {
    EditState* e = (EditState*)win->userdata;
    if (!e) return;

    int ch = win->h - 24; // content height

    if (key == KEY_ENTER) {
        edit_newline(e);
    } else if (key == KEY_BACKSPACE) {
        edit_backspace(e);
    } else if (key == KEY_LEFT) {
        if (e->cursor_x > 0) e->cursor_x--;
        else if (e->cursor_y > 0) {
            e->cursor_y--;
            e->cursor_x = strlen(e->lines[e->cursor_y]);
        }
    } else if (key == KEY_RIGHT) {
        int len = strlen(e->lines[e->cursor_y]);
        if (e->cursor_x < len) e->cursor_x++;
        else if (e->cursor_y < e->line_count - 1) {
            e->cursor_y++;
            e->cursor_x = 0;
        }
    } else if (key == KEY_UP) {
        if (e->cursor_y > 0) {
            e->cursor_y--;
            int len = strlen(e->lines[e->cursor_y]);
            if (e->cursor_x > len) e->cursor_x = len;
        }
    } else if (key == KEY_DOWN) {
        if (e->cursor_y < e->line_count - 1) {
            e->cursor_y++;
            int len = strlen(e->lines[e->cursor_y]);
            if (e->cursor_x > len) e->cursor_x = len;
        }
    } else if (key == KEY_HOME) {
        e->cursor_x = 0;
    } else if (key == KEY_END) {
        e->cursor_x = strlen(e->lines[e->cursor_y]);
    } else if (key == KEY_PGUP) {
        int vis = edit_visible_rows(ch);
        e->cursor_y -= vis;
        if (e->cursor_y < 0) e->cursor_y = 0;
        int len = strlen(e->lines[e->cursor_y]);
        if (e->cursor_x > len) e->cursor_x = len;
    } else if (key == KEY_PGDN) {
        int vis = edit_visible_rows(ch);
        e->cursor_y += vis;
        if (e->cursor_y >= e->line_count) e->cursor_y = e->line_count - 1;
        int len = strlen(e->lines[e->cursor_y]);
        if (e->cursor_x > len) e->cursor_x = len;
    } else if (key == KEY_TAB) {
        for (int i = 0; i < 4; i++) edit_insert_char(e, ' ');
    } else if (key >= 0x20 && key < 0x7F) {
        edit_insert_char(e, (char)key);
    }

    edit_ensure_visible(e, ch);
}

static void edit_close(Window* win) {
    if (win->userdata) kfree(win->userdata);
}

extern "C" void app_launch_edit() {
    Window* w = wm_create_window("Edit", 120, 70, 500, 350,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE | WIN_RESIZABLE);
    if (!w) return;

    EditState* e = (EditState*)kmalloc(sizeof(EditState));
    memset(e, 0, sizeof(EditState));
    e->line_count = 1;
    // Welcome text
    strcpy(e->lines[0], "Welcome to Gateway OS2 Edit.");
    strcpy(e->lines[1], "");
    strcpy(e->lines[2], "This is a simple text editor.");
    strcpy(e->lines[3], "Use arrow keys to navigate.");
    strcpy(e->lines[4], "");
    e->line_count = 5;

    w->userdata = e;
    w->on_draw = edit_draw;
    w->on_key = edit_key;
    w->on_close = edit_close;
}
