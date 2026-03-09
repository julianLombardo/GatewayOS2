#include "theme.h"
#include "font.h"
#include "../lib/string.h"

void nx_draw_raised(int x, int y, int w, int h) {
    nx_draw_raised_color(x, y, w, h, NX_LTGRAY);
}

void nx_draw_raised_color(int x, int y, int w, int h, uint32_t face) {
    // Fill
    fb_fillrect(x, y, w, h, face);
    // Outer black border
    fb_rect(x, y, w, h, NX_BLACK);
    // Inner highlight (top + left)
    fb_hline(x + 1, y + 1, w - 2, NX_WHITE);
    fb_vline(x + 1, y + 1, h - 2, NX_WHITE);
    // Inner shadow (bottom + right)
    fb_hline(x + 1, y + h - 2, w - 2, NX_DKGRAY);
    fb_vline(x + w - 2, y + 1, h - 2, NX_DKGRAY);
}

void nx_draw_sunken(int x, int y, int w, int h, uint32_t fill) {
    // Fill
    fb_fillrect(x, y, w, h, fill);
    // Outer black border
    fb_rect(x, y, w, h, NX_BLACK);
    // Inner shadow (top + left) -- reversed from raised
    fb_hline(x + 1, y + 1, w - 2, NX_DKGRAY);
    fb_vline(x + 1, y + 1, h - 2, NX_DKGRAY);
    // Inner highlight (bottom + right)
    fb_hline(x + 1, y + h - 2, w - 2, NX_WHITE);
    fb_vline(x + w - 2, y + 1, h - 2, NX_WHITE);
}

void nx_draw_bordered(int x, int y, int w, int h, uint32_t fill) {
    fb_fillrect(x, y, w, h, fill);
    fb_rect(x, y, w, h, NX_BLACK);
}

// Vertical scrollbar with both arrows at BOTTOM (NeXT signature style)
void nx_draw_vscrollbar(int x, int y, int h, int content_height, int scroll_pos, int visible_height) {
    int sb_w = 18;
    int arrow_h = 18;

    // Scrollbar trough (sunken)
    nx_draw_sunken(x, y, sb_w, h, NX_DKGRAY);

    // Arrow buttons at bottom
    int arrow_y = y + h - arrow_h * 2;

    // Up arrow button
    nx_draw_raised(x, arrow_y, sb_w, arrow_h);
    // Draw up triangle
    int cx = x + sb_w / 2;
    int cy = arrow_y + arrow_h / 2;
    for (int r = 0; r < 5; r++) {
        fb_hline(cx - r, cy - 2 + r, r * 2 + 1, NX_BLACK);
    }

    // Down arrow button
    nx_draw_raised(x, arrow_y + arrow_h, sb_w, arrow_h);
    cy = arrow_y + arrow_h + arrow_h / 2;
    for (int r = 0; r < 5; r++) {
        fb_hline(cx - r, cy + 2 - r, r * 2 + 1, NX_BLACK);
    }

    // Draw knob/thumb
    if (content_height > visible_height && content_height > 0) {
        int track_h = h - arrow_h * 2 - 4;
        int knob_h = (visible_height * track_h) / content_height;
        if (knob_h < 20) knob_h = 20;
        if (knob_h > track_h) knob_h = track_h;

        int max_scroll = content_height - visible_height;
        int knob_y = y + 2;
        if (max_scroll > 0)
            knob_y = y + 2 + ((scroll_pos * (track_h - knob_h)) / max_scroll);

        // Knob (raised)
        nx_draw_raised(x + 1, knob_y, sb_w - 2, knob_h);

        // Grip lines on knob
        int grip_y = knob_y + knob_h / 2;
        fb_hline(x + 5, grip_y - 2, sb_w - 10, NX_DKGRAY);
        fb_hline(x + 5, grip_y,     sb_w - 10, NX_DKGRAY);
        fb_hline(x + 5, grip_y + 2, sb_w - 10, NX_DKGRAY);
        fb_hline(x + 5, grip_y - 1, sb_w - 10, NX_WHITE);
        fb_hline(x + 5, grip_y + 1, sb_w - 10, NX_WHITE);
    }
}

void nx_draw_hscrollbar(int x, int y, int w, int content_width, int scroll_pos, int visible_width) {
    int sb_h = 18;
    int arrow_w = 18;

    // Trough
    nx_draw_sunken(x, y, w, sb_h, NX_DKGRAY);

    // Arrow buttons at RIGHT
    int arrow_x = x + w - arrow_w * 2;

    // Left arrow
    nx_draw_raised(arrow_x, y, arrow_w, sb_h);
    int cx = arrow_x + arrow_w / 2;
    int cy = y + sb_h / 2;
    for (int r = 0; r < 5; r++) {
        fb_vline(cx - 2 + r, cy - r, r * 2 + 1, NX_BLACK);
    }

    // Right arrow
    nx_draw_raised(arrow_x + arrow_w, y, arrow_w, sb_h);
    cx = arrow_x + arrow_w + arrow_w / 2;
    for (int r = 0; r < 5; r++) {
        fb_vline(cx + 2 - r, cy - r, r * 2 + 1, NX_BLACK);
    }

    // Knob
    if (content_width > visible_width && content_width > 0) {
        int track_w = w - arrow_w * 2 - 4;
        int knob_w = (visible_width * track_w) / content_width;
        if (knob_w < 20) knob_w = 20;
        if (knob_w > track_w) knob_w = track_w;

        int max_scroll = content_width - visible_width;
        int knob_x = x + 2;
        if (max_scroll > 0)
            knob_x = x + 2 + ((scroll_pos * (track_w - knob_w)) / max_scroll);

        nx_draw_raised(knob_x, y + 1, knob_w, sb_h - 2);
    }
}

void nx_draw_button(int x, int y, int w, int h, const char* label, bool pressed, bool is_default) {
    if (pressed) {
        // Sunken appearance
        fb_fillrect(x, y, w, h, NX_DKGRAY);
        fb_rect(x, y, w, h, NX_BLACK);
        fb_hline(x + 1, y + 1, w - 2, NX_DKGRAY);
        fb_vline(x + 1, y + 1, h - 2, NX_DKGRAY);
        fb_hline(x + 1, y + h - 2, w - 2, NX_WHITE);
        fb_vline(x + w - 2, y + 1, h - 2, NX_WHITE);

        int tw = font_string_width(label, FONT_MEDIUM);
        int tx = x + (w - tw) / 2 + 1;
        int ty = y + (h - font_char_height(FONT_MEDIUM)) / 2 + 1;
        font_draw_string_nobg(tx, ty, label, NX_WHITE, FONT_MEDIUM);
    } else {
        nx_draw_raised(x, y, w, h);
        int tw = font_string_width(label, FONT_MEDIUM);
        int tx = x + (w - tw) / 2;
        int ty = y + (h - font_char_height(FONT_MEDIUM)) / 2;
        font_draw_string_nobg(tx, ty, label, NX_BLACK, FONT_MEDIUM);
    }

    // Default button: extra thick black border
    if (is_default) {
        fb_rect(x - 1, y - 1, w + 2, h + 2, NX_BLACK);
        fb_rect(x - 2, y - 2, w + 4, h + 4, NX_BLACK);
    }
}

void nx_draw_textfield(int x, int y, int w, int h, const char* text, int cursor_pos, bool focused) {
    nx_draw_sunken(x, y, w, h, NX_WHITE);

    // Draw text
    int tx = x + 4;
    int ty = y + (h - font_char_height(FONT_MEDIUM)) / 2;
    if (text)
        font_draw_string_clipped(tx, ty, text, NX_BLACK, NX_WHITE, FONT_MEDIUM, w - 8);

    // Cursor
    if (focused && cursor_pos >= 0) {
        int cx = tx + cursor_pos * font_char_width(FONT_MEDIUM);
        if (cx < x + w - 4)
            fb_vline(cx, y + 3, h - 6, NX_BLACK);
    }

    // Focus ring
    if (focused) {
        fb_rect(x - 1, y - 1, w + 2, h + 2, NX_DKGRAY);
    }
}

void nx_draw_checkbox(int x, int y, const char* label, bool checked) {
    // 14x14 checkbox box
    nx_draw_sunken(x, y, 14, 14, NX_WHITE);
    if (checked) {
        // Draw checkmark
        for (int i = 0; i < 3; i++) {
            fb_putpixel(x + 3 + i, y + 7 + i, NX_BLACK);
            fb_putpixel(x + 4 + i, y + 7 + i, NX_BLACK);
        }
        for (int i = 0; i < 5; i++) {
            fb_putpixel(x + 6 + i, y + 9 - i, NX_BLACK);
            fb_putpixel(x + 7 + i, y + 9 - i, NX_BLACK);
        }
    }
    // Label
    font_draw_string_nobg(x + 18, y + 1, label, NX_BLACK, FONT_MEDIUM);
}

void nx_draw_radio(int x, int y, const char* label, bool selected) {
    // 14x14 radio area -- draw a circle approximation
    nx_draw_sunken(x, y, 14, 14, NX_WHITE);
    // Round corners
    fb_putpixel(x, y, NX_LTGRAY);
    fb_putpixel(x + 13, y, NX_LTGRAY);
    fb_putpixel(x, y + 13, NX_LTGRAY);
    fb_putpixel(x + 13, y + 13, NX_LTGRAY);

    if (selected) {
        // Filled dot in center
        fb_fillrect(x + 4, y + 4, 6, 6, NX_BLACK);
        fb_putpixel(x + 4, y + 4, NX_WHITE);
        fb_putpixel(x + 9, y + 4, NX_WHITE);
        fb_putpixel(x + 4, y + 9, NX_WHITE);
        fb_putpixel(x + 9, y + 9, NX_WHITE);
    }
    font_draw_string_nobg(x + 18, y + 1, label, NX_BLACK, FONT_MEDIUM);
}

void nx_draw_separator(int x, int y, int w) {
    fb_hline(x, y, w, NX_DKGRAY);
    fb_hline(x, y + 1, w, NX_WHITE);
}

void nx_draw_resize_handle(int x, int y) {
    // 12x12 resize grip with diagonal lines
    for (int i = 0; i < 4; i++) {
        int ox = x + 10 - i * 3;
        int oy = y + 1 + i * 3;
        for (int j = 0; j < 3 + i * 3 && ox + j < x + 12; j++) {
            if ((j + i) % 2 == 0)
                fb_putpixel(ox + j, oy, NX_DKGRAY);
        }
    }
}

void nx_draw_shadow(int x, int y, int w, int h, int offset) {
    uint32_t shadow = RGBA(0, 0, 0, 80);
    fb_fillrect_alpha(x + offset, y + h, w, offset, shadow);
    fb_fillrect_alpha(x + w, y + offset, offset, h, shadow);
}

void nx_draw_progress(int x, int y, int w, int h, int percent, uint32_t tick) {
    nx_draw_sunken(x, y, w, h, NX_WHITE);

    int fill_w = ((w - 4) * percent) / 100;
    if (fill_w <= 0) return;

    // Barber-pole pattern (NeXT signature)
    for (int px = 0; px < fill_w; px++) {
        for (int py = 0; py < h - 4; py++) {
            int stripe = ((px + py + (int)tick) / 4) % 2;
            uint32_t color = stripe ? NX_LTGRAY : NX_DKGRAY;
            fb_putpixel(x + 2 + px, y + 2 + py, color);
        }
    }
}
