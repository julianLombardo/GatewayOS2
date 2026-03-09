#include "apps.h"
#include "../memory/heap.h"
#include "../memory/pmm.h"

// ============================================================
// CALENDAR
// ============================================================
struct CalendarState {
    int month, year;
};

static int days_in_month(int m, int y) {
    int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))) return 29;
    return days[m - 1];
}

// Zeller's-like day of week (0=Sun)
static int day_of_week(int d, int m, int y) {
    if (m < 3) { m += 12; y--; }
    return (d + (13*(m+1))/5 + y + y/4 - y/100 + y/400) % 7;
}

static void cal_prev(CalendarState* c) {
    c->month--; if (c->month < 1) { c->month = 12; c->year--; }
}
static void cal_next(CalendarState* c) {
    c->month++; if (c->month > 12) { c->month = 1; c->year++; }
}

static void calendar_draw(Window* win, int cx, int cy, int cw, int ch) {
    CalendarState* c = (CalendarState*)win->userdata;
    if (!c) return;

    fb_fillrect(cx, cy, cw, ch, NX_WHITE);

    const char* months[] = {"","January","February","March","April","May","June",
                            "July","August","September","October","November","December"};

    // Header bar
    fb_fillrect(cx, cy, cw, 24, RGB(60, 100, 180));
    // Nav buttons
    nx_draw_button(cx + 4, cy + 3, 24, 18, "<", false, false);
    nx_draw_button(cx + cw - 28, cy + 3, 24, 18, ">", false, false);

    char title[32];
    ksprintf(title, "%s %d", months[c->month], c->year);
    int tw = strlen(title) * font_char_width(FONT_MEDIUM);
    font_draw_string(cx + (cw - tw) / 2, cy + 6, title, NX_WHITE, RGB(60, 100, 180), FONT_MEDIUM);

    // Day headers
    const char* days[] = {"Su","Mo","Tu","We","Th","Fr","Sa"};
    int cell_w = cw / 7;
    fb_fillrect(cx, cy + 24, cw, 16, RGB(230, 230, 240));
    for (int i = 0; i < 7; i++) {
        uint32_t col = (i == 0) ? RGB(200, 80, 80) : NX_DKGRAY;
        int tx = cx + i * cell_w + (cell_w - 2 * font_char_width(FONT_SMALL)) / 2;
        font_draw_string(tx, cy + 27, days[i], col, RGB(230, 230, 240), FONT_SMALL);
    }

    // Days grid
    int first = day_of_week(1, c->month, c->year);
    int total = days_in_month(c->month, c->year);
    int row = 0, col = first;
    int cell_h = 20;
    int grid_y = cy + 42;

    for (int d = 1; d <= total; d++) {
        int dx = cx + col * cell_w;
        int dy = grid_y + row * cell_h;

        // Alternate row shading
        if (row % 2 == 1)
            fb_fillrect(dx, dy, cell_w, cell_h, RGB(245, 245, 250));

        // Sunday highlight
        uint32_t num_col = (col == 0) ? RGB(200, 80, 80) : NX_BLACK;

        char num[4];
        ksprintf(num, "%d", d);
        int nw = strlen(num) * font_char_width(FONT_SMALL);
        font_draw_string(dx + (cell_w - nw) / 2, dy + 4, num, num_col,
                        (row % 2 == 1) ? RGB(245, 245, 250) : NX_WHITE, FONT_SMALL);

        // Grid lines
        fb_hline(dx, dy + cell_h - 1, cell_w, RGB(220, 220, 230));

        col++;
        if (col >= 7) { col = 0; row++; }
    }

    // Vertical grid lines
    for (int i = 1; i < 7; i++)
        fb_vline(cx + i * cell_w, grid_y, (row + 1) * cell_h, RGB(220, 220, 230));

    // Bottom status
    int by = cy + ch - 14;
    font_draw_string(cx + 6, by, "Click < > or arrow keys", NX_DKGRAY, NX_WHITE, FONT_SMALL);
}

static void calendar_key(Window* win, uint8_t key) {
    CalendarState* c = (CalendarState*)win->userdata;
    if (!c) return;
    if (key == KEY_LEFT) cal_prev(c);
    if (key == KEY_RIGHT) cal_next(c);
}

static void calendar_mouse(Window* win, int mx, int my, bool left, bool right) {
    (void)right;
    if (!left) return;
    CalendarState* c = (CalendarState*)win->userdata;
    if (!c) return;
    int cx, cy, cw, ch;
    wm_get_content_area(win, &cx, &cy, &cw, &ch);
    (void)ch;
    // Click on nav buttons
    if (my >= cy + 3 && my < cy + 21) {
        if (mx >= cx + 4 && mx < cx + 28) cal_prev(c);
        if (mx >= cx + cw - 28 && mx < cx + cw - 4) cal_next(c);
    }
}

static void calendar_close(Window* win) { if (win->userdata) kfree(win->userdata); }

extern "C" void app_launch_calendar() {
    Window* w = wm_create_window("Calendar", 220, 100, 280, 210,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE);
    if (!w) return;
    CalendarState* c = (CalendarState*)kmalloc(sizeof(CalendarState));
    c->month = 3; c->year = 2026;
    w->userdata = c;
    w->on_draw = calendar_draw;
    w->on_key = calendar_key;
    w->on_mouse = calendar_mouse;
    w->on_close = calendar_close;
}

// ============================================================
// NOTES - Quick sticky notes
// ============================================================
#define NOTES_MAX 512

struct NotesState {
    char text[NOTES_MAX];
    int len;
    int cursor;
    int scroll;
};

static void notes_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)ch;
    NotesState* n = (NotesState*)win->userdata;
    if (!n) return;

    fb_fillrect(cx, cy, cw, ch, RGB(255, 255, 180)); // Yellow sticky note

    // Draw text with word wrapping
    int x = cx + 6, y = cy + 6;
    int max_x = cx + cw - 6;
    int char_w = font_char_width(FONT_SMALL);
    for (int i = 0; i < n->len; i++) {
        if (n->text[i] == '\n' || x + char_w > max_x) {
            x = cx + 6;
            y += 11;
            if (n->text[i] == '\n') continue;
        }
        font_draw_char(x, y, n->text[i], NX_BLACK, RGB(255, 255, 180), FONT_SMALL);
        // Cursor
        if (i == n->cursor && (timer_get_ticks() / 50) % 2 == 0)
            fb_fillrect(x, y, 1, 9, NX_BLACK);
        x += char_w;
    }
    // Cursor at end
    if (n->cursor == n->len && (timer_get_ticks() / 50) % 2 == 0)
        fb_fillrect(x, y, 1, 9, NX_BLACK);
}

static void notes_key(Window* win, uint8_t key) {
    NotesState* n = (NotesState*)win->userdata;
    if (!n) return;
    if (key == KEY_ENTER && n->len < NOTES_MAX - 1) {
        // Insert newline at cursor
        for (int i = n->len; i > n->cursor; i--) n->text[i] = n->text[i-1];
        n->text[n->cursor] = '\n'; n->len++; n->cursor++;
        n->text[n->len] = 0;
    } else if (key == KEY_BACKSPACE && n->cursor > 0) {
        for (int i = n->cursor - 1; i < n->len; i++) n->text[i] = n->text[i+1];
        n->len--; n->cursor--;
    } else if (key == KEY_LEFT && n->cursor > 0) n->cursor--;
    else if (key == KEY_RIGHT && n->cursor < n->len) n->cursor++;
    else if (key >= 0x20 && key < 0x7F && n->len < NOTES_MAX - 1) {
        for (int i = n->len; i > n->cursor; i--) n->text[i] = n->text[i-1];
        n->text[n->cursor] = (char)key; n->len++; n->cursor++;
        n->text[n->len] = 0;
    }
}

static void notes_close(Window* win) { if (win->userdata) kfree(win->userdata); }

extern "C" void app_launch_notes() {
    Window* w = wm_create_window("Notes", 350, 200, 200, 180,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE | WIN_RESIZABLE);
    if (!w) return;
    NotesState* n = (NotesState*)kmalloc(sizeof(NotesState));
    memset(n, 0, sizeof(NotesState));
    w->userdata = n;
    w->on_draw = notes_draw;
    w->on_key = notes_key;
    w->on_close = notes_close;
}

// ============================================================
// CONTACTS - Simple address book
// ============================================================
struct Contact {
    char name[24];
    char email[32];
};

struct ContactsState {
    Contact contacts[10];
    int count;
    int selected;
};

static void contacts_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)ch;
    ContactsState* c = (ContactsState*)win->userdata;
    if (!c) return;

    fb_fillrect(cx, cy, cw, ch, NX_WHITE);

    // Header
    fb_fillrect(cx, cy, cw, 20, NX_LTGRAY);
    font_draw_string(cx + 8, cy + 4, "Contacts", NX_BLACK, NX_LTGRAY, FONT_MEDIUM);
    nx_draw_separator(cx, cy + 20, cw);

    for (int i = 0; i < c->count; i++) {
        int y = cy + 24 + i * 30;
        if (i == c->selected)
            fb_fillrect(cx + 1, y, cw - 2, 28, RGB(200, 210, 240));

        // Avatar circle
        uint32_t avatar_col = RGB(100 + (i * 40) % 155, 100 + (i * 70) % 155, 200);
        int ax = cx + 18, ay = y + 14;
        for (int dy = -8; dy <= 8; dy++)
            for (int dx = -8; dx <= 8; dx++)
                if (dx*dx + dy*dy <= 64) fb_putpixel(ax+dx, ay+dy, avatar_col);
        char initial[2] = {c->contacts[i].name[0], 0};
        font_draw_string(ax - 3, ay - 4, initial, NX_WHITE, avatar_col, FONT_SMALL);

        uint32_t bg = (i == c->selected) ? RGB(200, 210, 240) : NX_WHITE;
        font_draw_string(cx + 32, y + 4, c->contacts[i].name, NX_BLACK, bg, FONT_SMALL);
        font_draw_string(cx + 32, y + 16, c->contacts[i].email, NX_DKGRAY, bg, FONT_SMALL);
    }
}

static void contacts_key(Window* win, uint8_t key) {
    ContactsState* c = (ContactsState*)win->userdata;
    if (!c) return;
    if (key == KEY_UP && c->selected > 0) c->selected--;
    if (key == KEY_DOWN && c->selected < c->count - 1) c->selected++;
}

static void contacts_mouse(Window* win, int mx, int my, bool left, bool right) {
    (void)mx; (void)right;
    if (!left) return;
    ContactsState* c = (ContactsState*)win->userdata;
    if (!c) return;
    int cx, cy, cw, ch;
    wm_get_content_area(win, &cx, &cy, &cw, &ch);
    (void)cw;
    int rel = my - (cy + 24);
    if (rel >= 0) {
        int idx = rel / 30;
        if (idx >= 0 && idx < c->count) c->selected = idx;
    }
}

static void contacts_close(Window* win) { if (win->userdata) kfree(win->userdata); }

extern "C" void app_launch_contacts() {
    Window* w = wm_create_window("Contacts", 260, 120, 280, 220,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE);
    if (!w) return;
    ContactsState* c = (ContactsState*)kmalloc(sizeof(ContactsState));
    memset(c, 0, sizeof(ContactsState));
    strcpy(c->contacts[0].name, "Admin"); strcpy(c->contacts[0].email, "admin@gateway.os");
    strcpy(c->contacts[1].name, "System"); strcpy(c->contacts[1].email, "system@gateway.os");
    strcpy(c->contacts[2].name, "Root"); strcpy(c->contacts[2].email, "root@gateway.os");
    strcpy(c->contacts[3].name, "Guest"); strcpy(c->contacts[3].email, "guest@gateway.os");
    c->count = 4;
    w->userdata = c;
    w->on_draw = contacts_draw;
    w->on_mouse = contacts_mouse;
    w->on_key = contacts_key;
    w->on_close = contacts_close;
}

// ============================================================
// COLOR PICKER
// ============================================================
struct ColorPickState {
    int r, g, b;
    int field; // 0=R, 1=G, 2=B
};

static void colorpick_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)ch;
    ColorPickState* c = (ColorPickState*)win->userdata;
    if (!c) return;

    fb_fillrect(cx, cy, cw, ch, NX_LTGRAY);

    // Preview swatch
    uint32_t color = RGB(c->r, c->g, c->b);
    nx_draw_sunken(cx + 8, cy + 8, 80, 60, color);
    fb_fillrect(cx + 10, cy + 10, 76, 56, color);

    // Hex value
    char hex[16];
    ksprintf(hex, "#%02X%02X%02X", c->r, c->g, c->b);
    font_draw_string(cx + 100, cy + 10, hex, NX_BLACK, NX_LTGRAY, FONT_MEDIUM);

    char rgb[32];
    ksprintf(rgb, "R:%d G:%d B:%d", c->r, c->g, c->b);
    font_draw_string(cx + 100, cy + 30, rgb, NX_BLACK, NX_LTGRAY, FONT_SMALL);

    // Sliders
    const char* labels[] = {"Red", "Green", "Blue"};
    int* vals[] = {&c->r, &c->g, &c->b};
    uint32_t cols[] = {RGB(255,0,0), RGB(0,255,0), RGB(0,0,255)};

    for (int i = 0; i < 3; i++) {
        int sy = cy + 80 + i * 24;
        bool active = (c->field == i);
        font_draw_string(cx + 8, sy + 2, labels[i], active ? NX_BLACK : NX_DKGRAY, NX_LTGRAY, FONT_SMALL);

        int bar_x = cx + 52, bar_w = cw - 100;
        nx_draw_sunken(bar_x, sy, bar_w, 16, NX_WHITE);
        int fill = *vals[i] * (bar_w - 4) / 255;
        fb_fillrect(bar_x + 2, sy + 2, fill, 12, cols[i]);

        char val[8];
        ksprintf(val, "%3d", *vals[i]);
        font_draw_string(cx + cw - 40, sy + 2, val, NX_BLACK, NX_LTGRAY, FONT_SMALL);

        if (active) fb_rect(bar_x - 1, sy - 1, bar_w + 2, 18, NX_BLACK);
    }

    font_draw_string(cx + 8, cy + ch - 14, "Up/Down: select  Left/Right: adjust", NX_DKGRAY, NX_LTGRAY, FONT_SMALL);
}

static void colorpick_key(Window* win, uint8_t key) {
    ColorPickState* c = (ColorPickState*)win->userdata;
    if (!c) return;
    if (key == KEY_UP) { c->field--; if (c->field < 0) c->field = 2; }
    if (key == KEY_DOWN) { c->field++; if (c->field > 2) c->field = 0; }
    int* val = (c->field == 0) ? &c->r : (c->field == 1) ? &c->g : &c->b;
    if (key == KEY_LEFT) { *val -= 5; if (*val < 0) *val = 0; }
    if (key == KEY_RIGHT) { *val += 5; if (*val > 255) *val = 255; }
}

static void colorpick_mouse(Window* win, int mx, int my, bool left, bool right) {
    (void)right;
    if (!left) return;
    ColorPickState* c = (ColorPickState*)win->userdata;
    if (!c) return;
    int cx, cy, cw, ch;
    wm_get_content_area(win, &cx, &cy, &cw, &ch);
    (void)ch;

    // Click on slider bars
    for (int i = 0; i < 3; i++) {
        int sy = cy + 80 + i * 24;
        int bar_x = cx + 52, bar_w = cw - 100;
        if (my >= sy && my < sy + 16 && mx >= bar_x && mx < bar_x + bar_w) {
            c->field = i;
            int* val = (i == 0) ? &c->r : (i == 1) ? &c->g : &c->b;
            *val = (mx - bar_x - 2) * 255 / (bar_w - 4);
            if (*val < 0) *val = 0;
            if (*val > 255) *val = 255;
            return;
        }
    }
}

static void colorpick_close(Window* win) { if (win->userdata) kfree(win->userdata); }

extern "C" void app_launch_colorpick() {
    Window* w = wm_create_window("Color Picker", 250, 150, 250, 170,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE);
    if (!w) return;
    ColorPickState* c = (ColorPickState*)kmalloc(sizeof(ColorPickState));
    c->r = 100; c->g = 150; c->b = 200; c->field = 0;
    w->userdata = c;
    w->on_draw = colorpick_draw;
    w->on_mouse = colorpick_mouse;
    w->on_key = colorpick_key;
    w->on_close = colorpick_close;
}

// ============================================================
// FONT VIEWER
// ============================================================
static void fontview_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)win; (void)ch;
    fb_fillrect(cx, cy, cw, ch, NX_WHITE);

    font_draw_string(cx + 8, cy + 4, "Font Viewer", NX_BLACK, NX_WHITE, FONT_MEDIUM);
    nx_draw_separator(cx + 4, cy + 22, cw - 8);

    // Show all printable ASCII in each size
    int y = cy + 30;
    font_draw_string(cx + 8, y, "FONT_SMALL (8px):", NX_DKGRAY, NX_WHITE, FONT_SMALL); y += 14;
    font_draw_string(cx + 8, y, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", NX_BLACK, NX_WHITE, FONT_SMALL); y += 12;
    font_draw_string(cx + 8, y, "abcdefghijklmnopqrstuvwxyz", NX_BLACK, NX_WHITE, FONT_SMALL); y += 12;
    font_draw_string(cx + 8, y, "0123456789 !@#$%^&*()-=+[]", NX_BLACK, NX_WHITE, FONT_SMALL); y += 18;

    font_draw_string(cx + 8, y, "FONT_MEDIUM (12px):", NX_DKGRAY, NX_WHITE, FONT_SMALL); y += 14;
    font_draw_string(cx + 8, y, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", NX_BLACK, NX_WHITE, FONT_MEDIUM); y += 16;
    font_draw_string(cx + 8, y, "abcdefghijklmnopqrstuvwxyz", NX_BLACK, NX_WHITE, FONT_MEDIUM); y += 16;
    font_draw_string(cx + 8, y, "0123456789 !@#$%^&*()", NX_BLACK, NX_WHITE, FONT_MEDIUM); y += 22;

    font_draw_string(cx + 8, y, "FONT_LARGE (16px):", NX_DKGRAY, NX_WHITE, FONT_SMALL); y += 14;
    font_draw_string(cx + 8, y, "ABCDEFGHIJKLMNO", NX_BLACK, NX_WHITE, FONT_LARGE); y += 20;
    font_draw_string(cx + 8, y, "abcdefghijklmno", NX_BLACK, NX_WHITE, FONT_LARGE); y += 20;
    font_draw_string(cx + 8, y, "0123456789", NX_BLACK, NX_WHITE, FONT_LARGE);
}

extern "C" void app_launch_fontview() {
    Window* w = wm_create_window("Font Viewer", 150, 80, 350, 280,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE);
    if (!w) return;
    w->on_draw = fontview_draw;
}

// ============================================================
// TASK MANAGER - Shows all windows with kill option
// ============================================================
static void taskmgr_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)ch;
    fb_fillrect(cx, cy, cw, ch, NX_LTGRAY);

    font_draw_string(cx + 8, cy + 4, "Task Manager", NX_BLACK, NX_LTGRAY, FONT_MEDIUM);
    nx_draw_separator(cx + 4, cy + 22, cw - 8);

    // Header
    font_draw_string(cx + 8, cy + 28, "ID", NX_DKGRAY, NX_LTGRAY, FONT_SMALL);
    font_draw_string(cx + 40, cy + 28, "Title", NX_DKGRAY, NX_LTGRAY, FONT_SMALL);
    font_draw_string(cx + cw - 60, cy + 28, "Status", NX_DKGRAY, NX_LTGRAY, FONT_SMALL);
    fb_hline(cx + 4, cy + 40, cw - 8, NX_DKGRAY);

    int y = cy + 44;
    for (int i = 0; i < wm_get_count() && y < cy + ch - 20; i++) {
        Window* w = wm_get_by_index(i);
        if (!w) continue;

        // Don't list ourselves
        char buf[8];
        ksprintf(buf, "%d", w->id);
        font_draw_string(cx + 8, y, buf, NX_BLACK, NX_LTGRAY, FONT_SMALL);
        font_draw_string(cx + 40, y, w->title, NX_BLACK, NX_LTGRAY, FONT_SMALL);

        const char* status = (w->flags & WIN_MINIMIZED) ? "Hidden" : "Active";
        uint32_t scol = (w->flags & WIN_MINIMIZED) ? NX_DKGRAY : NX_GREEN;
        font_draw_string(cx + cw - 60, y, status, scol, NX_LTGRAY, FONT_SMALL);
        y += 14;
    }

    char buf[32];
    ksprintf(buf, "Total: %d windows", wm_get_count());
    font_draw_string(cx + 8, cy + ch - 16, buf, NX_DKGRAY, NX_LTGRAY, FONT_SMALL);
}

extern "C" void app_launch_taskmgr() {
    Window* w = wm_create_window("Task Manager", 180, 100, 320, 260,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE | WIN_RESIZABLE);
    if (!w) return;
    w->on_draw = taskmgr_draw;
}

// ============================================================
// DISK USAGE - Show memory/storage usage visualization
// ============================================================
static void diskuse_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)win; (void)ch;
    fb_fillrect(cx, cy, cw, ch, NX_LTGRAY);

    font_draw_string(cx + 8, cy + 4, "Disk & Memory Usage", NX_BLACK, NX_LTGRAY, FONT_MEDIUM);
    nx_draw_separator(cx + 4, cy + 22, cw - 8);

    // Physical memory pie-chart style
    uint32_t total = pmm_get_total_pages();
    uint32_t used = pmm_get_used_pages();
    uint32_t free_p = total - used;

    int pie_cx = cx + 70, pie_cy = cy + 90, pie_r = 45;

    // Draw pie: used portion
    int used_angle = (int)(used * 360 / total);
    for (int a = 0; a < 360; a++) {
        uint32_t col = (a < used_angle) ? RGB(220, 80, 80) : RGB(80, 200, 80);
        for (int r = 10; r < pie_r; r++) {
            int px = pie_cx + r * cos256(a - 90) / 256;
            int py = pie_cy + r * sin256(a - 90) / 256;
            fb_putpixel(px, py, col);
        }
    }

    // Legend
    int ly = cy + 50;
    fb_fillrect(cx + 150, ly, 12, 12, RGB(220, 80, 80));
    fb_rect(cx + 150, ly, 12, 12, NX_BLACK);
    char buf[48];
    ksprintf(buf, "Used: %d KB", used * 4);
    font_draw_string(cx + 168, ly + 1, buf, NX_BLACK, NX_LTGRAY, FONT_SMALL);

    ly += 18;
    fb_fillrect(cx + 150, ly, 12, 12, RGB(80, 200, 80));
    fb_rect(cx + 150, ly, 12, 12, NX_BLACK);
    ksprintf(buf, "Free: %d KB", free_p * 4);
    font_draw_string(cx + 168, ly + 1, buf, NX_BLACK, NX_LTGRAY, FONT_SMALL);

    ly += 18;
    ksprintf(buf, "Total: %d KB", total * 4);
    font_draw_string(cx + 150, ly + 1, buf, NX_DKGRAY, NX_LTGRAY, FONT_SMALL);

    // Heap bar
    int hy = cy + 150;
    font_draw_string(cx + 8, hy, "Heap:", NX_BLACK, NX_LTGRAY, FONT_SMALL);
    uint32_t h_used = heap_get_used();
    uint32_t h_total = heap_get_total();
    int bar_w = cw - 60;
    nx_draw_sunken(cx + 50, hy, bar_w, 14, NX_WHITE);
    int fill = (int)(h_used * (uint32_t)(bar_w - 4) / h_total);
    fb_fillrect(cx + 52, hy + 2, fill, 10, RGB(100, 100, 220));
    ksprintf(buf, "%d / %d bytes", h_used, h_total);
    font_draw_string(cx + 8, hy + 18, buf, NX_DKGRAY, NX_LTGRAY, FONT_SMALL);
}

extern "C" void app_launch_diskuse() {
    Window* w = wm_create_window("Disk Usage", 200, 110, 310, 200,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE);
    if (!w) return;
    w->on_draw = diskuse_draw;
}

// ============================================================
// LOG VIEWER - Shows kernel serial log
// ============================================================
static void logview_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)win; (void)ch;
    fb_fillrect(cx, cy, cw, ch, NX_BLACK);

    font_draw_string(cx + 4, cy + 2, "System Log", NX_GREEN, NX_BLACK, FONT_SMALL);
    fb_hline(cx + 2, cy + 14, cw - 4, RGB(0, 80, 0));

    // Show boot log entries (hardcoded since we don't buffer serial output)
    int y = cy + 18;
    const char* log[] = {
        "[GW2] Gateway OS2 booting...",
        "[GW2] GDT initialized",
        "[GW2] IDT/ISR/IRQ initialized",
        "[GW2] Timer + Keyboard initialized",
        "[GW2] PMM initialized",
        "[GW2] Heap initialized (4 MB)",
        "[GW2] Mouse initialized",
        "[GW2] Setting up VESA framebuffer",
        "[GW2] Bochs VBE fallback active",
        "[GW2] Desktop environment ready",
        NULL
    };
    for (int i = 0; log[i] && y < cy + ch - 10; i++) {
        font_draw_string(cx + 4, y, log[i], NX_GREEN, NX_BLACK, FONT_SMALL);
        y += 11;
    }

    y += 6;
    // Live info
    char buf[48];
    ksprintf(buf, "[LIVE] Ticks: %d", timer_get_ticks());
    font_draw_string(cx + 4, y, buf, RGB(0, 255, 0), NX_BLACK, FONT_SMALL); y += 11;
    ksprintf(buf, "[LIVE] Windows: %d", wm_get_count());
    font_draw_string(cx + 4, y, buf, RGB(0, 255, 0), NX_BLACK, FONT_SMALL); y += 11;
    ksprintf(buf, "[LIVE] Heap used: %d bytes", heap_get_used());
    font_draw_string(cx + 4, y, buf, RGB(0, 255, 0), NX_BLACK, FONT_SMALL);
}

extern "C" void app_launch_logview() {
    Window* w = wm_create_window("System Log", 130, 70, 340, 220,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE | WIN_RESIZABLE);
    if (!w) return;
    w->on_draw = logview_draw;
}

// ============================================================
// SCREENSAVER - Bouncing logo screensaver
// ============================================================
struct ScreensaverState {
    int x, y, dx, dy;
    uint32_t color;
    int color_idx;
};

static void screensaver_draw(Window* win, int cx, int cy, int cw, int ch) {
    ScreensaverState* s = (ScreensaverState*)win->userdata;
    if (!s) return;

    fb_fillrect(cx, cy, cw, ch, NX_BLACK);

    // Update position
    s->x += s->dx;
    s->y += s->dy;

    int tw = 120, th = 16;
    if (s->x <= 0 || s->x + tw >= cw) {
        s->dx = -s->dx;
        s->color_idx = (s->color_idx + 1) % 6;
        uint32_t colors[] = {RGB(255,0,0), RGB(0,255,0), RGB(0,100,255), RGB(255,255,0), RGB(255,0,255), RGB(0,255,255)};
        s->color = colors[s->color_idx];
    }
    if (s->y <= 0 || s->y + th >= ch) {
        s->dy = -s->dy;
        s->color_idx = (s->color_idx + 1) % 6;
        uint32_t colors[] = {RGB(255,0,0), RGB(0,255,0), RGB(0,100,255), RGB(255,255,0), RGB(255,0,255), RGB(0,255,255)};
        s->color = colors[s->color_idx];
    }

    font_draw_string_nobg(cx + s->x, cy + s->y, "GATEWAY OS2", s->color, FONT_LARGE);

    font_draw_string(cx + 4, cy + ch - 12, "Press any key to exit", RGB(40, 40, 40), NX_BLACK, FONT_SMALL);
}

static void screensaver_key(Window* win, uint8_t key) {
    (void)key;
    wm_close_window(win->id);
}

static void screensaver_close(Window* win) { if (win->userdata) kfree(win->userdata); }

extern "C" void app_launch_screensaver() {
    Window* w = wm_create_window("Screensaver", 100, 60, 500, 350,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE);
    if (!w) return;
    ScreensaverState* s = (ScreensaverState*)kmalloc(sizeof(ScreensaverState));
    s->x = 50; s->y = 50; s->dx = 2; s->dy = 1;
    s->color = RGB(0, 255, 0); s->color_idx = 1;
    w->userdata = s;
    w->on_draw = screensaver_draw;
    w->on_key = screensaver_key;
    w->on_close = screensaver_close;
}

// ============================================================
// WEATHER - Simulated weather display
// ============================================================
static void weather_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)win; (void)ch;
    fb_fillrect(cx, cy, cw, ch, RGB(100, 160, 220));

    // Sun
    int sx = cx + cw - 60, sy = cy + 40;
    for (int dy = -20; dy <= 20; dy++)
        for (int dx = -20; dx <= 20; dx++)
            if (dx*dx + dy*dy <= 400)
                fb_putpixel(sx+dx, sy+dy, RGB(255, 220, 50));
    // Rays
    for (int a = 0; a < 360; a += 30) {
        for (int r = 24; r < 32; r++) {
            int px = sx + r * cos256(a) / 256;
            int py = sy + r * sin256(a) / 256;
            fb_putpixel(px, py, RGB(255, 220, 50));
        }
    }

    // Cloud
    int cloud_x = cx + 60, cloud_y = cy + 35;
    for (int dy = -10; dy <= 10; dy++)
        for (int dx = -25; dx <= 25; dx++)
            if (dx*dx*256/(25*25) + dy*dy*256/(10*10) < 256)
                fb_putpixel(cloud_x+dx, cloud_y+dy, NX_WHITE);

    // Temperature
    font_draw_string(cx + 20, cy + 65, "72 F", NX_WHITE, RGB(100, 160, 220), FONT_LARGE);
    font_draw_string(cx + 20, cy + 90, "Partly Cloudy", NX_WHITE, RGB(100, 160, 220), FONT_MEDIUM);

    nx_draw_separator(cx + 8, cy + 110, cw - 16);

    // Forecast
    const char* days[] = {"Mon", "Tue", "Wed", "Thu", "Fri"};
    const char* temps[] = {"74", "68", "71", "65", "73"};
    int fw = (cw - 16) / 5;
    for (int i = 0; i < 5; i++) {
        int fx = cx + 8 + i * fw;
        font_draw_string(fx + 4, cy + 118, days[i], NX_WHITE, RGB(100, 160, 220), FONT_SMALL);
        font_draw_string(fx + 4, cy + 132, temps[i], NX_WHITE, RGB(100, 160, 220), FONT_MEDIUM);
    }

    font_draw_string(cx + 8, cy + ch - 14, "Simulated - no network", RGB(60, 120, 180), RGB(100, 160, 220), FONT_SMALL);
}

extern "C" void app_launch_weather() {
    Window* w = wm_create_window("Weather", 280, 160, 260, 170,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE);
    if (!w) return;
    w->on_draw = weather_draw;
}

// ============================================================
// MUSIC PLAYER - PC speaker music player
// ============================================================
struct MusicState {
    int track;
    bool playing;
    int note_idx;
    uint32_t last_note;
};

static void music_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)ch;
    MusicState* m = (MusicState*)win->userdata;
    if (!m) return;

    fb_fillrect(cx, cy, cw, ch, RGB(30, 30, 40));

    // Album art area
    nx_draw_sunken(cx + 8, cy + 8, 80, 80, RGB(50, 50, 70));
    // Music note icon
    for (int dy = 0; dy < 30; dy++)
        fb_putpixel(cx + 55, cy + 25 + dy, NX_WHITE);
    for (int dx = -8; dx <= 0; dx++)
        for (int dy = -4; dy <= 4; dy++)
            if (dx*dx + dy*dy <= 32)
                fb_putpixel(cx + 55 + dx, cy + 55 + dy, NX_WHITE);

    // Track info
    const char* tracks[] = {"Boot Melody", "Startup Chime", "Alert Tone"};
    font_draw_string(cx + 100, cy + 15, "Now Playing:", RGB(150, 150, 180), RGB(30, 30, 40), FONT_SMALL);
    font_draw_string(cx + 100, cy + 30, tracks[m->track], NX_WHITE, RGB(30, 30, 40), FONT_MEDIUM);
    font_draw_string(cx + 100, cy + 50, "Gateway OS2 Sounds", RGB(120, 120, 150), RGB(30, 30, 40), FONT_SMALL);

    // Controls
    const char* status = m->playing ? "Playing" : "Stopped";
    font_draw_string(cx + 100, cy + 70, status, m->playing ? NX_GREEN : NX_RED, RGB(30, 30, 40), FONT_SMALL);

    // Buttons
    int by = cy + 100;
    nx_draw_raised_color(cx + 30, by, 50, 22, NX_DKGRAY);
    font_draw_string(cx + 42, by + 6, "Prev", NX_WHITE, NX_DKGRAY, FONT_SMALL);
    nx_draw_raised_color(cx + 90, by, 60, 22, NX_DKGRAY);
    font_draw_string(cx + 98, by + 6, m->playing ? "Stop" : "Play", NX_WHITE, NX_DKGRAY, FONT_SMALL);
    nx_draw_raised_color(cx + 160, by, 50, 22, NX_DKGRAY);
    font_draw_string(cx + 172, by + 6, "Next", NX_WHITE, NX_DKGRAY, FONT_SMALL);

    font_draw_string(cx + 8, cy + ch - 14, "Keys: P=play/stop  <>=tracks", RGB(80, 80, 100), RGB(30, 30, 40), FONT_SMALL);
}

static void music_key(Window* win, uint8_t key) {
    MusicState* m = (MusicState*)win->userdata;
    if (!m) return;
    if (key == 'p' || key == 'P') m->playing = !m->playing;
    if (key == KEY_LEFT || key == ',') { m->track--; if (m->track < 0) m->track = 2; }
    if (key == KEY_RIGHT || key == '.') { m->track++; if (m->track > 2) m->track = 0; }
}

static void music_mouse(Window* win, int mx, int my, bool left, bool right) {
    (void)right;
    if (!left) return;
    MusicState* m = (MusicState*)win->userdata;
    if (!m) return;
    int cx, cy, cw, ch;
    wm_get_content_area(win, &cx, &cy, &cw, &ch);
    (void)cw;
    int by = cy + 100;
    if (my >= by && my < by + 22) {
        if (mx >= cx + 30 && mx < cx + 80)  { m->track--; if (m->track < 0) m->track = 2; }
        if (mx >= cx + 90 && mx < cx + 150) { m->playing = !m->playing; }
        if (mx >= cx + 160 && mx < cx + 210) { m->track++; if (m->track > 2) m->track = 0; }
    }
}

static void music_close(Window* win) { if (win->userdata) kfree(win->userdata); }

extern "C" void app_launch_music() {
    Window* w = wm_create_window("Music Player", 200, 130, 260, 145,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE);
    if (!w) return;
    MusicState* m = (MusicState*)kmalloc(sizeof(MusicState));
    memset(m, 0, sizeof(MusicState));
    w->userdata = m;
    w->on_draw = music_draw;
    w->on_mouse = music_mouse;
    w->on_key = music_key;
    w->on_close = music_close;
}

// ============================================================
// FILE VIEWER - Clickable directory listing
// ============================================================
struct FileViewState {
    int selected;
};

static const char* fv_names[] = {
    "/apps", "/boot", "/drivers", "/gui", "/kernel", "/lib", "/memory",
    "/gateway2.elf", "/linker.ld", "/Makefile"
};
static const char* fv_types[] = {
    "drwx", "drwx", "drwx", "drwx", "drwx", "drwx", "drwx",
    "-rw-", "-rw-", "-rw-"
};
static const char* fv_sizes[] = {
    "<DIR>", "<DIR>", "<DIR>", "<DIR>", "<DIR>", "<DIR>", "<DIR>",
    "102K", "1K", "2K"
};

static void fileview_draw(Window* win, int cx, int cy, int cw, int ch) {
    FileViewState* fv = (FileViewState*)win->userdata;
    int sel = fv ? fv->selected : -1;

    fb_fillrect(cx, cy, cw, ch, NX_WHITE);

    fb_fillrect(cx, cy, cw, 20, NX_LTGRAY);
    font_draw_string(cx + 8, cy + 4, "File Viewer - /", NX_BLACK, NX_LTGRAY, FONT_SMALL);
    nx_draw_separator(cx, cy + 20, cw);

    // Column headers
    int y = cy + 24;
    font_draw_string(cx + 8, y, "Perm", NX_DKGRAY, NX_WHITE, FONT_SMALL);
    font_draw_string(cx + 50, y, "Name", NX_DKGRAY, NX_WHITE, FONT_SMALL);
    font_draw_string(cx + cw - 50, y, "Size", NX_DKGRAY, NX_WHITE, FONT_SMALL);
    fb_hline(cx + 4, y + 12, cw - 8, NX_LTGRAY);
    y += 16;

    for (int i = 0; i < 10 && y < cy + ch - 20; i++) {
        uint32_t bg = (i == sel) ? RGB(200, 210, 240) : NX_WHITE;
        fb_fillrect(cx + 1, y - 1, cw - 2, 14, bg);

        bool is_dir = (fv_types[i][0] == 'd');
        uint32_t name_col = is_dir ? RGB(0, 0, 180) : NX_BLACK;

        font_draw_string(cx + 8, y, fv_types[i], NX_DKGRAY, bg, FONT_SMALL);
        // Small icon
        if (is_dir) {
            fb_fillrect(cx + 40, y + 1, 8, 3, RGB(255, 200, 80));
            fb_fillrect(cx + 38, y + 4, 12, 7, RGB(255, 220, 100));
        } else {
            fb_fillrect(cx + 40, y + 1, 8, 10, NX_WHITE);
            fb_rect(cx + 40, y + 1, 8, 10, NX_DKGRAY);
        }
        font_draw_string(cx + 56, y, fv_names[i], name_col, bg, FONT_SMALL);
        font_draw_string(cx + cw - 50, y, fv_sizes[i], NX_DKGRAY, bg, FONT_SMALL);
        y += 14;
    }

    // Status bar
    fb_fillrect(cx, cy + ch - 16, cw, 16, NX_LTGRAY);
    font_draw_string(cx + 8, cy + ch - 13, "10 items  (read-only virtual FS)", NX_DKGRAY, NX_LTGRAY, FONT_SMALL);
}

static void fileview_mouse(Window* win, int mx, int my, bool left, bool right) {
    (void)mx; (void)right;
    if (!left) return;
    FileViewState* fv = (FileViewState*)win->userdata;
    if (!fv) return;
    int cx, cy, cw, ch;
    wm_get_content_area(win, &cx, &cy, &cw, &ch);
    (void)cw;
    int rel_y = my - (cy + 40);
    if (rel_y >= 0) {
        int idx = rel_y / 14;
        if (idx >= 0 && idx < 10) fv->selected = idx;
    }
}

static void fileview_key(Window* win, uint8_t key) {
    FileViewState* fv = (FileViewState*)win->userdata;
    if (!fv) return;
    if (key == KEY_UP && fv->selected > 0) fv->selected--;
    if (key == KEY_DOWN && fv->selected < 9) fv->selected++;
}

static void fileview_close(Window* win) { if (win->userdata) kfree(win->userdata); }

extern "C" void app_launch_fileview() {
    Window* w = wm_create_window("File Viewer", 160, 80, 300, 280,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE);
    if (!w) return;
    FileViewState* fv = (FileViewState*)kmalloc(sizeof(FileViewState));
    fv->selected = -1;
    w->userdata = fv;
    w->on_draw = fileview_draw;
    w->on_mouse = fileview_mouse;
    w->on_key = fileview_key;
    w->on_close = fileview_close;
}
