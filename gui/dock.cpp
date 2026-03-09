#include "dock.h"
#include "theme.h"
#include "font.h"
#include "../drivers/framebuffer.h"
#include "../lib/string.h"
#include "../lib/math.h"

static DockItem dock_items[DOCK_MAX_ITEMS];
static int dock_count = 0;

void dock_init() {
    memset(dock_items, 0, sizeof(dock_items));
    dock_count = 0;
}

void dock_add_item(const char* name, void (*launcher)(), const uint8_t* icon) {
    if (dock_count >= DOCK_MAX_ITEMS) return;
    DockItem* item = &dock_items[dock_count++];
    strncpy(item->name, name, 23);
    item->launcher = launcher;
    item->running = false;
    item->is_separator = false;
    if (icon) {
        memcpy(item->icon, icon, DOCK_ICON_SIZE * DOCK_ICON_SIZE);
    } else {
        memset(item->icon, 0, sizeof(item->icon));
    }
}

void dock_add_separator() {
    if (dock_count >= DOCK_MAX_ITEMS) return;
    DockItem* item = &dock_items[dock_count++];
    memset(item, 0, sizeof(DockItem));
    item->is_separator = true;
}

void dock_set_running(const char* name, bool running) {
    for (int i = 0; i < dock_count; i++) {
        if (strcmp(dock_items[i].name, name) == 0) {
            dock_items[i].running = running;
            return;
        }
    }
}

int dock_get_x() {
    return SCREEN_WIDTH - DOCK_TILE_SIZE - 1;
}

void dock_draw() {
    int x = dock_get_x();
    int y = 0;

    // Draw dock background (full-height strip on right)
    fb_fillrect(x, 0, DOCK_TILE_SIZE + 1, SCREEN_HEIGHT, NX_DKGRAY);
    // Left border
    fb_vline(x, 0, SCREEN_HEIGHT, NX_BLACK);

    for (int i = 0; i < dock_count; i++) {
        DockItem* item = &dock_items[i];

        if (item->is_separator) {
            // Thin separator line
            nx_draw_separator(x + 4, y + 2, DOCK_TILE_SIZE - 8);
            y += 5;
            continue;
        }

        // Tile background (raised 3D)
        nx_draw_raised_color(x + 1, y + 1, DOCK_TILE_SIZE - 1, DOCK_TILE_SIZE - 1, NX_LTGRAY);

        // Draw icon with distinct shape per app
        int icon_x = x + (DOCK_TILE_SIZE - DOCK_ICON_SIZE) / 2 + 1;
        int icon_y = y + (DOCK_TILE_SIZE - DOCK_ICON_SIZE) / 2 + 1;

        // App-specific color palette
        uint32_t icon_bg, icon_fg, icon_accent;
        const char* n = item->name;

        if (strcmp(n, "Terminal") == 0) {
            icon_bg = RGB(30, 30, 30); icon_fg = NX_GREEN; icon_accent = RGB(0, 200, 0);
        } else if (strcmp(n, "Edit") == 0) {
            icon_bg = NX_WHITE; icon_fg = NX_BLACK; icon_accent = RGB(100, 100, 220);
        } else if (strcmp(n, "Java") == 0) {
            icon_bg = RGB(200, 80, 60); icon_fg = NX_WHITE; icon_accent = RGB(240, 120, 80);
        } else if (strcmp(n, "Gmail") == 0) {
            icon_bg = NX_WHITE; icon_fg = RGB(220, 50, 50); icon_accent = RGB(60, 120, 220);
        } else if (strcmp(n, "Calc") == 0) {
            icon_bg = RGB(60, 60, 80); icon_fg = NX_WHITE; icon_accent = NX_AMBER;
        } else if (strcmp(n, "SysMon") == 0) {
            icon_bg = RGB(20, 40, 20); icon_fg = NX_GREEN; icon_accent = RGB(0, 180, 0);
        } else if (strcmp(n, "Notes") == 0) {
            icon_bg = RGB(255, 255, 180); icon_fg = NX_BLACK; icon_accent = RGB(200, 200, 100);
        } else if (strcmp(n, "Chess") == 0) {
            icon_bg = RGB(200, 170, 120); icon_fg = NX_BLACK; icon_accent = NX_WHITE;
        } else if (strcmp(n, "Paint") == 0) {
            icon_bg = NX_WHITE; icon_fg = RGB(220, 50, 50); icon_accent = RGB(50, 150, 220);
        } else if (strcmp(n, "Files") == 0) {
            icon_bg = RGB(255, 220, 100); icon_fg = RGB(180, 140, 40); icon_accent = NX_BLACK;
        } else if (strcmp(n, "Prefs") == 0) {
            icon_bg = NX_LTGRAY; icon_fg = NX_DKGRAY; icon_accent = NX_BLACK;
        } else {
            icon_bg = RGB(80, 90, 120); icon_fg = NX_WHITE; icon_accent = NX_LTGRAY;
        }

        // Icon background with rounded look
        int ix = icon_x + 4, iy = icon_y + 4, iw = 40, ih = 40;
        fb_fillrect(ix + 1, iy, iw - 2, ih, icon_bg);
        fb_fillrect(ix, iy + 1, iw, ih - 2, icon_bg);
        fb_rect(ix + 1, iy, iw - 2, ih, NX_BLACK);
        fb_vline(ix, iy + 1, ih - 2, NX_BLACK);
        fb_vline(ix + iw - 1, iy + 1, ih - 2, NX_BLACK);
        // Highlight
        fb_hline(ix + 2, iy + 1, iw - 4, RGB(
            (COLOR_R(icon_bg) + 255) / 2, (COLOR_G(icon_bg) + 255) / 2, (COLOR_B(icon_bg) + 255) / 2));

        // Draw app-specific icon glyph
        int cx2 = ix + iw / 2, cy2 = iy + ih / 2;
        if (strcmp(n, "Terminal") == 0) {
            // Terminal: ">_" prompt
            font_draw_string_nobg(ix + 6, iy + 8, ">_", icon_fg, FONT_LARGE);
            fb_hline(ix + 6, iy + 30, 28, RGB(0, 100, 0));
        } else if (strcmp(n, "Edit") == 0) {
            // Text editor: lines of text
            for (int l = 0; l < 5; l++) {
                int lw = (l == 2) ? 22 : (l == 4) ? 16 : 28;
                fb_hline(ix + 6, iy + 8 + l * 6, lw, NX_DKGRAY);
            }
            fb_vline(ix + 4, iy + 6, 26, icon_accent);
        } else if (strcmp(n, "Java") == 0) {
            // Coffee cup
            font_draw_string_nobg(ix + 8, iy + 6, "J", icon_fg, FONT_LARGE);
            fb_fillrect(ix + 10, iy + 22, 20, 12, icon_accent);
            fb_rect(ix + 10, iy + 22, 20, 12, NX_BLACK);
        } else if (strcmp(n, "Gmail") == 0) {
            // Envelope
            fb_fillrect(ix + 6, iy + 12, 28, 18, NX_WHITE);
            fb_rect(ix + 6, iy + 12, 28, 18, RGB(200, 50, 50));
            // V flap
            for (int d = 0; d < 9; d++) {
                fb_putpixel(ix + 6 + d, iy + 12 + d, RGB(220, 50, 50));
                fb_putpixel(ix + 33 - d, iy + 12 + d, RGB(220, 50, 50));
            }
        } else if (strcmp(n, "Calc") == 0) {
            // Calculator grid
            fb_fillrect(ix + 8, iy + 6, 24, 10, RGB(80, 100, 80));
            font_draw_string_nobg(ix + 10, iy + 7, "42", NX_GREEN, FONT_SMALL);
            for (int r = 0; r < 3; r++)
                for (int c = 0; c < 3; c++)
                    fb_fillrect(ix + 8 + c * 8, iy + 19 + r * 7, 6, 5, NX_LTGRAY);
        } else if (strcmp(n, "SysMon") == 0) {
            // CPU graph
            int pts[] = {28, 20, 25, 15, 22, 10, 18, 24, 16, 22};
            for (int p = 0; p < 9; p++) {
                int x1 = ix + 4 + p * 3, y1 = iy + pts[p];
                int x2 = ix + 4 + (p+1) * 3, y2 = iy + pts[p+1];
                fb_hline(x1, y1, x2 - x1, icon_fg);
                if (y2 < y1) fb_vline(x2, y2, y1 - y2, icon_fg);
                else fb_vline(x2, y1, y2 - y1, icon_fg);
            }
        } else if (strcmp(n, "Notes") == 0) {
            // Sticky note lines
            for (int l = 0; l < 4; l++)
                fb_hline(ix + 8, iy + 10 + l * 7, 24, RGB(180, 180, 100));
            fb_fillrect(ix + iw - 10, iy + ih - 10, 10, 10, RGB(220, 220, 140));
        } else if (strcmp(n, "Chess") == 0) {
            // Chess board 4x4 mini
            for (int r = 0; r < 4; r++)
                for (int c = 0; c < 4; c++) {
                    uint32_t sq = ((r + c) % 2 == 0) ? NX_WHITE : NX_BLACK;
                    fb_fillrect(ix + 8 + c * 6, iy + 8 + r * 6, 6, 6, sq);
                }
            fb_rect(ix + 8, iy + 8, 24, 24, NX_BLACK);
        } else if (strcmp(n, "Paint") == 0) {
            // Color palette
            uint32_t cols[] = {RGB(220,50,50), RGB(50,180,50), RGB(50,100,220), RGB(240,200,50)};
            for (int c = 0; c < 4; c++) {
                int px = ix + 6 + (c % 2) * 14, py = iy + 6 + (c / 2) * 14;
                fb_fillrect(px, py, 12, 12, cols[c]);
                fb_rect(px, py, 12, 12, NX_BLACK);
            }
        } else if (strcmp(n, "Files") == 0) {
            // Folder
            fb_fillrect(ix + 8, iy + 10, 12, 4, icon_fg);
            fb_fillrect(ix + 6, iy + 14, 28, 18, icon_bg);
            fb_rect(ix + 6, iy + 14, 28, 18, NX_BLACK);
        } else if (strcmp(n, "Prefs") == 0) {
            // Gear (simplified)
            for (int a = 0; a < 360; a += 2) {
                int r2 = (a % 45 < 22) ? 14 : 10;
                int gx = cx2 + r2 * cos256(a) / 256;
                int gy = cy2 + r2 * sin256(a) / 256;
                fb_putpixel(gx, gy, NX_DKGRAY);
            }
            for (int dy = -4; dy <= 4; dy++)
                for (int dx = -4; dx <= 4; dx++)
                    if (dx*dx + dy*dy <= 16)
                        fb_putpixel(cx2+dx, cy2+dy, NX_BLACK);
        } else {
            // Default: app abbreviation
            char abbr[4] = {0};
            abbr[0] = item->name[0];
            if (item->name[1]) abbr[1] = item->name[1];
            int tw2 = font_string_width(abbr, FONT_LARGE);
            font_draw_string_nobg(ix + (iw - tw2) / 2, iy + (ih - 16) / 2, abbr, icon_fg, FONT_LARGE);
        }

        // Running indicator (three dots at bottom)
        if (item->running) {
            int dx = x + DOCK_TILE_SIZE / 2;
            int dy = y + DOCK_TILE_SIZE - 6;
            fb_fillrect(dx - 5, dy, 2, 2, NX_BLACK);
            fb_fillrect(dx - 1, dy, 2, 2, NX_BLACK);
            fb_fillrect(dx + 3, dy, 2, 2, NX_BLACK);
        }

        y += DOCK_TILE_SIZE;
    }
}

void dock_launch(int index) {
    if (index < 0 || index >= dock_count) return;
    if (dock_items[index].is_separator) return;
    if (dock_items[index].launcher) dock_items[index].launcher();
}

int dock_hit_test(int mx, int my) {
    int x = dock_get_x();
    if (mx < x) return -1;

    int y = 0;
    for (int i = 0; i < dock_count; i++) {
        if (dock_items[i].is_separator) {
            y += 5;
            continue;
        }
        if (my >= y && my < y + DOCK_TILE_SIZE)
            return i;
        y += DOCK_TILE_SIZE;
    }
    return -1;
}
