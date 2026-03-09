#include "apps.h"

struct ClockState {
    uint32_t last_tick;
};

static void clock_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)ch;
    fb_fillrect(cx, cy, cw, ch, NX_LTGRAY);

    uint32_t ticks = timer_get_ticks();
    uint32_t total_secs = ticks / 100;
    uint32_t hrs = (total_secs / 3600) % 24;
    uint32_t mins = (total_secs / 60) % 60;
    uint32_t secs = total_secs % 60;

    // Analog clock face
    int center_x = cx + cw / 2;
    int center_y = cy + 80;
    int radius = 60;

    // Clock face (sunken circle area)
    nx_draw_sunken(center_x - radius - 4, center_y - radius - 4,
                   radius * 2 + 8, radius * 2 + 8, NX_WHITE);
    // White face
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dy * dy <= radius * radius)
                fb_putpixel(center_x + dx, center_y + dy, NX_WHITE);
        }
    }

    // Hour markers
    for (int h = 0; h < 12; h++) {
        int angle = h * 30 - 90;
        int x1 = center_x + (radius - 8) * cos256(angle) / 256;
        int y1 = center_y + (radius - 8) * sin256(angle) / 256;
        int x2 = center_x + (radius - 2) * cos256(angle) / 256;
        int y2 = center_y + (radius - 2) * sin256(angle) / 256;
        // Draw thick marker
        for (int d = -1; d <= 1; d++) {
            fb_putpixel(x1 + d, y1, NX_BLACK);
            fb_putpixel(x2 + d, y2, NX_BLACK);
            fb_putpixel(x1, y1 + d, NX_BLACK);
            fb_putpixel(x2, y2 + d, NX_BLACK);
        }
    }

    // Hour hand
    int h_angle = ((int)hrs % 12) * 30 + (int)mins / 2 - 90;
    for (int r = 0; r < radius * 50 / 100; r++) {
        int hx = center_x + r * cos256(h_angle) / 256;
        int hy = center_y + r * sin256(h_angle) / 256;
        fb_putpixel(hx, hy, NX_BLACK);
        fb_putpixel(hx + 1, hy, NX_BLACK);
        fb_putpixel(hx, hy + 1, NX_BLACK);
    }

    // Minute hand
    int m_angle = (int)mins * 6 - 90;
    for (int r = 0; r < radius * 75 / 100; r++) {
        int mx2 = center_x + r * cos256(m_angle) / 256;
        int my2 = center_y + r * sin256(m_angle) / 256;
        fb_putpixel(mx2, my2, NX_BLACK);
        fb_putpixel(mx2 + 1, my2, NX_BLACK);
    }

    // Second hand
    int s_angle = (int)secs * 6 - 90;
    for (int r = 0; r < radius * 85 / 100; r++) {
        int sx = center_x + r * cos256(s_angle) / 256;
        int sy = center_y + r * sin256(s_angle) / 256;
        fb_putpixel(sx, sy, NX_RED);
    }

    // Center dot
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++)
            if (dx * dx + dy * dy <= 4)
                fb_putpixel(center_x + dx, center_y + dy, NX_BLACK);

    // Digital time below
    char time_str[16];
    ksprintf(time_str, "%02d:%02d:%02d", hrs, mins, secs);
    int tw = strlen(time_str) * font_char_width(FONT_LARGE);
    font_draw_string(cx + (cw - tw) / 2, cy + 155, time_str, NX_BLACK, NX_LTGRAY, FONT_LARGE);

    // Uptime label
    char uptime[32];
    ksprintf(uptime, "Uptime: %ds", total_secs);
    int uw = strlen(uptime) * font_char_width(FONT_SMALL);
    font_draw_string(cx + (cw - uw) / 2, cy + 180, uptime, NX_DKGRAY, NX_LTGRAY, FONT_SMALL);
}

extern "C" void app_launch_clock() {
    Window* w = wm_create_window("Clock", 300, 150, 180, 200,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE);
    if (!w) return;
    w->on_draw = clock_draw;
}
