#include "apps.h"
#include "../memory/heap.h"

struct CalcState {
    char display[32];
    int32_t accumulator;
    int32_t operand;
    char op;
    bool new_input;
    bool has_op;
};

static const char* calc_buttons[] = {
    "C", "+/-", "%", "/",
    "7", "8",   "9", "*",
    "4", "5",   "6", "-",
    "1", "2",   "3", "+",
    "0", ".",   "=", ""
};

static void calc_update_display(CalcState* c, int32_t val) {
    ksprintf(c->display, "%d", val);
}

static void calc_do_op(CalcState* c) {
    switch (c->op) {
        case '+': c->accumulator += c->operand; break;
        case '-': c->accumulator -= c->operand; break;
        case '*': c->accumulator *= c->operand; break;
        case '/': if (c->operand != 0) c->accumulator /= c->operand; break;
        case '%': if (c->operand != 0) c->accumulator %= c->operand; break;
    }
    calc_update_display(c, c->accumulator);
}

static void calc_press(CalcState* c, const char* btn) {
    if (btn[0] >= '0' && btn[0] <= '9') {
        if (c->new_input) {
            c->operand = 0;
            c->new_input = false;
        }
        c->operand = c->operand * 10 + (btn[0] - '0');
        calc_update_display(c, c->operand);
    } else if (strcmp(btn, "C") == 0) {
        c->accumulator = 0;
        c->operand = 0;
        c->op = 0;
        c->has_op = false;
        c->new_input = true;
        strcpy(c->display, "0");
    } else if (strcmp(btn, "+/-") == 0) {
        c->operand = -c->operand;
        calc_update_display(c, c->operand);
    } else if (strcmp(btn, "=") == 0) {
        if (c->has_op) {
            calc_do_op(c);
            c->has_op = false;
            c->operand = c->accumulator;
            c->new_input = true;
        }
    } else if (btn[0] == '+' || btn[0] == '-' || btn[0] == '*' || btn[0] == '/' || btn[0] == '%') {
        if (c->has_op) {
            calc_do_op(c);
        } else {
            c->accumulator = c->operand;
        }
        c->op = btn[0];
        c->has_op = true;
        c->new_input = true;
    }
}

static void calc_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)ch;
    CalcState* c = (CalcState*)win->userdata;
    if (!c) return;

    fb_fillrect(cx, cy, cw, ch, NX_LTGRAY);

    // Display area (sunken)
    nx_draw_sunken(cx + 8, cy + 8, cw - 16, 28, NX_WHITE);
    // Right-align display text
    int tw = strlen(c->display) * font_char_width(FONT_MEDIUM);
    font_draw_string(cx + cw - 20 - tw, cy + 14, c->display, NX_BLACK, NX_WHITE, FONT_MEDIUM);

    // Buttons: 5 rows x 4 cols
    int bw = (cw - 40) / 4;
    int bh = 28;
    int startx = cx + 8;
    int starty = cy + 44;

    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 4; col++) {
            int idx = row * 4 + col;
            if (idx >= 20 || calc_buttons[idx][0] == 0) continue;

            int bx = startx + col * (bw + 4);
            int by = starty + row * (bh + 4);

            // Special wide 0 button
            int w = bw;
            if (row == 4 && col == 0) w = bw; // Normal width

            bool is_op = (col == 3 && row < 4) || (strcmp(calc_buttons[idx], "=") == 0);
            uint32_t bg = is_op ? NX_DKGRAY : NX_LTGRAY;
            uint32_t fg = is_op ? NX_WHITE : NX_BLACK;

            nx_draw_raised_color(bx, by, w, bh, bg);
            int tw2 = strlen(calc_buttons[idx]) * font_char_width(FONT_SMALL);
            font_draw_string(bx + (w - tw2) / 2, by + 10, calc_buttons[idx], fg, bg, FONT_SMALL);
        }
    }
}

static void calc_mouse(Window* win, int mx, int my, bool left, bool right) {
    (void)right;
    CalcState* c = (CalcState*)win->userdata;
    if (!c || !left) return;

    int cx = win->x + 1;  // content origin
    int cy = win->y + 24; // below title bar

    int bw = (win->w - 2 - 40) / 4;
    int bh = 28;
    int startx = cx + 8;
    int starty = cy + 44;

    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 4; col++) {
            int idx = row * 4 + col;
            if (idx >= 20 || calc_buttons[idx][0] == 0) continue;

            int bx = startx + col * (bw + 4);
            int by = starty + row * (bh + 4);

            if (mx >= bx && mx < bx + bw && my >= by && my < by + bh) {
                calc_press(c, calc_buttons[idx]);
                return;
            }
        }
    }
}

static void calc_key(Window* win, uint8_t key) {
    CalcState* c = (CalcState*)win->userdata;
    if (!c) return;

    char btn[4] = {0};
    if (key >= '0' && key <= '9') { btn[0] = key; calc_press(c, btn); }
    else if (key == '+') calc_press(c, "+");
    else if (key == '-') calc_press(c, "-");
    else if (key == '*') calc_press(c, "*");
    else if (key == '/') calc_press(c, "/");
    else if (key == '%') calc_press(c, "%");
    else if (key == KEY_ENTER || key == '=') calc_press(c, "=");
    else if (key == KEY_ESCAPE) calc_press(c, "C");
}

static void calc_close(Window* win) {
    if (win->userdata) kfree(win->userdata);
}

extern "C" void app_launch_calculator() {
    Window* w = wm_create_window("Calculator", 200, 100, 220, 230,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE);
    if (!w) return;

    CalcState* c = (CalcState*)kmalloc(sizeof(CalcState));
    memset(c, 0, sizeof(CalcState));
    strcpy(c->display, "0");
    c->new_input = true;

    w->userdata = c;
    w->on_draw = calc_draw;
    w->on_key = calc_key;
    w->on_mouse = calc_mouse;
    w->on_close = calc_close;
}
