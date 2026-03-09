#include "apps.h"
#include "../memory/heap.h"
#include "../memory/pmm.h"
#include "../drivers/ports.h"

// ============================================================
// GW-DECRYPT - Interactive cipher/decryption tool
// ============================================================
struct DecryptState {
    char input[256];
    char output[256];
    int input_len;
    int mode; // 0=ROT13, 1=Caesar, 2=Reverse, 3=XOR
    int shift;
    uint32_t anim_tick;
    int anim_pos;
};

static void decrypt_process(DecryptState* d) {
    int len = d->input_len;
    for (int i = 0; i < len; i++) {
        char c = d->input[i];
        switch (d->mode) {
            case 0: // ROT13
                if (c >= 'a' && c <= 'z') d->output[i] = 'a' + (c - 'a' + 13) % 26;
                else if (c >= 'A' && c <= 'Z') d->output[i] = 'A' + (c - 'A' + 13) % 26;
                else d->output[i] = c;
                break;
            case 1: // Caesar
                if (c >= 'a' && c <= 'z') d->output[i] = 'a' + (c - 'a' + d->shift) % 26;
                else if (c >= 'A' && c <= 'Z') d->output[i] = 'A' + (c - 'A' + d->shift) % 26;
                else d->output[i] = c;
                break;
            case 2: // Reverse
                d->output[i] = d->input[len - 1 - i];
                break;
            case 3: // XOR
                d->output[i] = c ^ (d->shift & 0xFF);
                if (d->output[i] < 0x20 || d->output[i] > 0x7E)
                    d->output[i] = '.';
                break;
        }
    }
    d->output[len] = 0;
}

static void decrypt_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)ch;
    DecryptState* d = (DecryptState*)win->userdata;
    if (!d) return;

    fb_fillrect(cx, cy, cw, ch, NX_BLACK);

    // Header
    font_draw_string(cx + 8, cy + 4, "GW-DECRYPT v2.0", RGB(0, 255, 0), NX_BLACK, FONT_MEDIUM);
    fb_hline(cx + 4, cy + 20, cw - 8, RGB(0, 100, 0));

    // Mode selector
    const char* modes[] = {"ROT13", "Caesar", "Reverse", "XOR"};
    font_draw_string(cx + 8, cy + 28, "Mode:", RGB(0, 180, 0), NX_BLACK, FONT_SMALL);
    for (int i = 0; i < 4; i++) {
        uint32_t col = (i == d->mode) ? RGB(0, 255, 0) : RGB(0, 80, 0);
        char buf[16];
        ksprintf(buf, "[%d]%s", i + 1, modes[i]);
        font_draw_string(cx + 50 + i * 75, cy + 28, buf, col, NX_BLACK, FONT_SMALL);
    }

    if (d->mode == 1 || d->mode == 3) {
        char buf[32];
        ksprintf(buf, "Shift/Key: %d (Up/Down)", d->shift);
        font_draw_string(cx + 8, cy + 42, buf, RGB(0, 150, 0), NX_BLACK, FONT_SMALL);
    }

    // Input field
    font_draw_string(cx + 8, cy + 58, "INPUT:", RGB(0, 200, 0), NX_BLACK, FONT_SMALL);
    fb_rect(cx + 8, cy + 70, cw - 16, 20, RGB(0, 100, 0));
    font_draw_string(cx + 12, cy + 74, d->input, RGB(0, 255, 0), NX_BLACK, FONT_SMALL);
    // Cursor
    if ((timer_get_ticks() / 50) % 2 == 0) {
        int cx2 = cx + 12 + d->input_len * font_char_width(FONT_SMALL);
        font_draw_char(cx2, cy + 74, '_', RGB(0, 255, 0), NX_BLACK, FONT_SMALL);
    }

    // Output
    font_draw_string(cx + 8, cy + 98, "OUTPUT:", RGB(0, 200, 0), NX_BLACK, FONT_SMALL);
    fb_rect(cx + 8, cy + 110, cw - 16, 20, RGB(0, 100, 0));

    // Animated output reveal
    uint32_t now = timer_get_ticks();
    if (now > d->anim_tick + 2) {
        d->anim_tick = now;
        if (d->anim_pos < d->input_len) d->anim_pos++;
    }
    char partial[256];
    memcpy(partial, d->output, d->anim_pos);
    partial[d->anim_pos] = 0;
    font_draw_string(cx + 12, cy + 114, partial, RGB(255, 200, 0), NX_BLACK, FONT_SMALL);

    // Hex dump
    font_draw_string(cx + 8, cy + 140, "HEX:", RGB(0, 150, 0), NX_BLACK, FONT_SMALL);
    char hex[256] = {0};
    int hi = 0;
    for (int i = 0; i < d->input_len && hi < 200; i++) {
        char h[4];
        ksprintf(h, "%02X ", (uint8_t)d->output[i]);
        memcpy(hex + hi, h, 3);
        hi += 3;
    }
    hex[hi] = 0;
    font_draw_string(cx + 8, cy + 152, hex, RGB(0, 200, 0), NX_BLACK, FONT_SMALL);
}

static void decrypt_key(Window* win, uint8_t key) {
    DecryptState* d = (DecryptState*)win->userdata;
    if (!d) return;

    if (key == '1') d->mode = 0;
    else if (key == '2') d->mode = 1;
    else if (key == '3') d->mode = 2;
    else if (key == '4') d->mode = 3;
    else if (key == KEY_UP) d->shift++;
    else if (key == KEY_DOWN) { d->shift--; if (d->shift < 0) d->shift = 0; }
    else if (key == KEY_BACKSPACE && d->input_len > 0) {
        d->input[--d->input_len] = 0;
    } else if (key >= 0x20 && key < 0x7F && d->input_len < 60) {
        d->input[d->input_len++] = (char)key;
        d->input[d->input_len] = 0;
    }
    d->anim_pos = 0;
    decrypt_process(d);
}

static void decrypt_close(Window* win) { if (win->userdata) kfree(win->userdata); }

extern "C" void app_launch_decrypt() {
    Window* w = wm_create_window("GW-Decrypt", 100, 80, 420, 230,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE);
    if (!w) return;
    DecryptState* d = (DecryptState*)kmalloc(sizeof(DecryptState));
    memset(d, 0, sizeof(DecryptState));
    d->shift = 3;
    w->userdata = d;
    w->on_draw = decrypt_draw;
    w->on_key = decrypt_key;
    w->on_close = decrypt_close;
}

// ============================================================
// GW-RADAR - Radar sweep visualization
// ============================================================
struct RadarState {
    int angle;
    int blips[8][2]; // x,y positions
    int blip_count;
    uint32_t last_tick;
};

static void radar_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)cw; (void)ch;
    RadarState* r = (RadarState*)win->userdata;
    if (!r) return;

    fb_fillrect(cx, cy, cw, ch, NX_BLACK);

    int center_x = cx + cw / 2;
    int center_y = cy + ch / 2;
    int radius = (cw < ch ? cw : ch) / 2 - 20;

    // Update
    uint32_t now = timer_get_ticks();
    if (now - r->last_tick >= 3) {
        r->last_tick = now;
        r->angle = (r->angle + 2) % 360;
        // Randomly add/remove blips
        if (rand() % 200 == 0 && r->blip_count < 8) {
            int a = rand() % 360;
            int d = rand() % (radius - 10) + 10;
            r->blips[r->blip_count][0] = cos256(a) * d / 256;
            r->blips[r->blip_count][1] = sin256(a) * d / 256;
            r->blip_count++;
        }
    }

    // Radar circles
    for (int rr = radius / 4; rr <= radius; rr += radius / 4) {
        for (int a = 0; a < 360; a += 2) {
            int x = center_x + rr * cos256(a) / 256;
            int y = center_y + rr * sin256(a) / 256;
            fb_putpixel(x, y, RGB(0, 60, 0));
        }
    }

    // Cross hairs
    fb_hline(center_x - radius, center_y, radius * 2, RGB(0, 60, 0));
    fb_vline(center_x, center_y - radius, radius * 2, RGB(0, 60, 0));

    // Sweep line with fade trail
    for (int trail = 0; trail < 30; trail++) {
        int a = r->angle - trail;
        if (a < 0) a += 360;
        int brightness = 255 - trail * 8;
        if (brightness < 0) brightness = 0;
        uint32_t col = RGB(0, brightness, 0);
        for (int d = 0; d < radius; d += 2) {
            int x = center_x + d * cos256(a) / 256;
            int y = center_y + d * sin256(a) / 256;
            fb_putpixel(x, y, col);
        }
    }

    // Blips
    for (int i = 0; i < r->blip_count; i++) {
        int bx = center_x + r->blips[i][0];
        int by = center_y + r->blips[i][1];
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++)
                if (dx * dx + dy * dy <= 4)
                    fb_putpixel(bx + dx, by + dy, RGB(0, 255, 0));
    }

    // Label
    font_draw_string(cx + 4, cy + 4, "GW-RADAR", RGB(0, 200, 0), NX_BLACK, FONT_SMALL);
    char buf[16];
    ksprintf(buf, "%d blips", r->blip_count);
    font_draw_string(cx + 4, cy + ch - 14, buf, RGB(0, 150, 0), NX_BLACK, FONT_SMALL);
}

static void radar_key(Window* win, uint8_t key) {
    RadarState* r = (RadarState*)win->userdata;
    if (r && key == 'c') r->blip_count = 0;
}

static void radar_close(Window* win) { if (win->userdata) kfree(win->userdata); }

extern "C" void app_launch_radar() {
    Window* w = wm_create_window("GW-Radar", 200, 100, 280, 280,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE);
    if (!w) return;
    RadarState* r = (RadarState*)kmalloc(sizeof(RadarState));
    memset(r, 0, sizeof(RadarState));
    r->last_tick = timer_get_ticks();
    w->userdata = r;
    w->on_draw = radar_draw;
    w->on_key = radar_key;
    w->on_close = radar_close;
}

// ============================================================
// GW-NEURAL - Neural network visualization
// ============================================================
struct NeuralState {
    int weights[3][4]; // 3 layers, 4 nodes (weights * 256)
    int activations[3][4];
    uint32_t last_tick;
    int training_step;
};

static void neural_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)cw; (void)ch;
    NeuralState* n = (NeuralState*)win->userdata;
    if (!n) return;

    fb_fillrect(cx, cy, cw, ch, NX_BLACK);
    font_draw_string(cx + 8, cy + 4, "GW-NEURAL v2.0", RGB(0, 200, 255), NX_BLACK, FONT_MEDIUM);

    // Update activations
    uint32_t now = timer_get_ticks();
    if (now - n->last_tick >= 10) {
        n->last_tick = now;
        n->training_step++;
        for (int l = 0; l < 3; l++)
            for (int nd = 0; nd < 4; nd++) {
                n->weights[l][nd] = (n->weights[l][nd] + rand() % 20 - 10);
                if (n->weights[l][nd] > 255) n->weights[l][nd] = 255;
                if (n->weights[l][nd] < 0) n->weights[l][nd] = 0;
                n->activations[l][nd] = (sin256(n->training_step * 3 + l * 40 + nd * 30) + 256) / 2;
            }
    }

    // Draw network nodes
    int layer_x[3] = {cx + 60, cx + cw / 2, cx + cw - 60};
    int nodes[3] = {4, 4, 3};

    // Draw connections first
    for (int l = 0; l < 2; l++) {
        for (int i = 0; i < nodes[l]; i++) {
            int y1 = cy + 40 + i * 45;
            for (int j = 0; j < nodes[l + 1]; j++) {
                int y2 = cy + 40 + j * 45;
                int w = n->weights[l][i];
                uint32_t col = RGB(0, w / 3, w);
                // Draw line
                int dx = layer_x[l + 1] - layer_x[l];
                int dy = y2 - y1;
                int steps = (abs(dx) > abs(dy)) ? abs(dx) : abs(dy);
                if (steps == 0) steps = 1;
                for (int s = 0; s < steps; s += 2) {
                    int px = layer_x[l] + dx * s / steps;
                    int py = y1 + dy * s / steps;
                    fb_putpixel(px, py, col);
                }
            }
        }
    }

    // Draw nodes
    for (int l = 0; l < 3; l++) {
        for (int i = 0; i < nodes[l]; i++) {
            int nx2 = layer_x[l];
            int ny2 = cy + 40 + i * 45;
            int act = n->activations[l][i];
            uint32_t col = RGB(act, act / 2, 255 - act);
            for (int dy = -8; dy <= 8; dy++)
                for (int dx = -8; dx <= 8; dx++)
                    if (dx * dx + dy * dy <= 64)
                        fb_putpixel(nx2 + dx, ny2 + dy, col);
            // Value
            char buf[8];
            ksprintf(buf, "%d", act * 100 / 256);
            font_draw_string(nx2 - 10, ny2 - 4, buf, NX_WHITE, col, FONT_SMALL);
        }
    }

    // Labels
    font_draw_string(cx + 30, cy + ch - 30, "Input", RGB(0, 150, 255), NX_BLACK, FONT_SMALL);
    font_draw_string(cx + cw / 2 - 15, cy + ch - 30, "Hidden", RGB(0, 150, 255), NX_BLACK, FONT_SMALL);
    font_draw_string(cx + cw - 80, cy + ch - 30, "Output", RGB(0, 150, 255), NX_BLACK, FONT_SMALL);

    char buf[32];
    ksprintf(buf, "Step: %d", n->training_step);
    font_draw_string(cx + 8, cy + ch - 14, buf, RGB(0, 100, 200), NX_BLACK, FONT_SMALL);
}

static void neural_close(Window* win) { if (win->userdata) kfree(win->userdata); }

extern "C" void app_launch_neural() {
    Window* w = wm_create_window("GW-Neural", 150, 70, 350, 250,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE);
    if (!w) return;
    NeuralState* n = (NeuralState*)kmalloc(sizeof(NeuralState));
    memset(n, 0, sizeof(NeuralState));
    for (int l = 0; l < 3; l++)
        for (int nd = 0; nd < 4; nd++)
            n->weights[l][nd] = rand() % 256;
    n->last_tick = timer_get_ticks();
    w->userdata = n;
    w->on_draw = neural_draw;
    w->on_close = neural_close;
}

// ============================================================
// GW-MATRIX - Matrix rain effect
// ============================================================
struct MatrixAppState {
    char chars[64][48]; // columns x rows
    int heads[64];
    int lengths[64];
    int speeds[64];
    int ticks[64];
    int cols, rows;
};

static void matrix_draw(Window* win, int cx, int cy, int cw, int ch) {
    MatrixAppState* m = (MatrixAppState*)win->userdata;
    if (!m) return;

    fb_fillrect(cx, cy, cw, ch, NX_BLACK);

    m->cols = cw / 8;
    m->rows = ch / 8;
    if (m->cols > 64) m->cols = 64;
    if (m->rows > 48) m->rows = 48;

    for (int c = 0; c < m->cols; c++) {
        m->ticks[c]++;
        if (m->ticks[c] >= m->speeds[c]) {
            m->ticks[c] = 0;
            m->heads[c]++;
            if (m->heads[c] - m->lengths[c] > m->rows) {
                m->heads[c] = -(int)(rand() % 8);
                m->lengths[c] = 4 + rand() % 16;
                m->speeds[c] = 1 + rand() % 3;
            }
        }
        for (int r = 0; r < m->rows; r++) {
            int dist = m->heads[c] - r;
            if (dist < 0 || dist > m->lengths[c]) continue;
            if (rand() % 6 == 0) {
                int v = rand() % 62;
                m->chars[c][r] = v < 10 ? '0' + v : (v < 36 ? 'A' + v - 10 : 'a' + v - 36);
            }
            uint32_t color;
            if (dist == 0) color = RGB(220, 255, 220);
            else if (dist < 3) color = RGB(0, 255, 0);
            else {
                int fade = 255 - dist * 200 / m->lengths[c];
                if (fade < 30) fade = 30;
                color = RGB(0, fade, 0);
            }
            if (m->chars[c][r])
                font_draw_char_nobg(cx + c * 8, cy + r * 8, m->chars[c][r], color, FONT_SMALL);
        }
    }
}

static void matrix_close(Window* win) { if (win->userdata) kfree(win->userdata); }

extern "C" void app_launch_matrix() {
    Window* w = wm_create_window("GW-Matrix", 200, 80, 350, 300,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE | WIN_RESIZABLE);
    if (!w) return;
    MatrixAppState* m = (MatrixAppState*)kmalloc(sizeof(MatrixAppState));
    memset(m, 0, sizeof(MatrixAppState));
    for (int c = 0; c < 64; c++) {
        m->heads[c] = -(int)(rand() % 20);
        m->lengths[c] = 4 + rand() % 16;
        m->speeds[c] = 1 + rand() % 3;
    }
    w->userdata = m;
    w->on_draw = matrix_draw;
    w->on_close = matrix_close;
}

// ============================================================
// GW-UPLINK - Network uplink visualization
// ============================================================
struct UplinkState {
    int packets_sent;
    int packets_recv;
    int bandwidth;
    int signal_strength;
    int data_stream[40];
    uint32_t last_tick;
    bool connected;
};

static void uplink_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)ch;
    UplinkState* u = (UplinkState*)win->userdata;
    if (!u) return;

    fb_fillrect(cx, cy, cw, ch, NX_BLACK);
    font_draw_string(cx + 8, cy + 4, "GW-UPLINK v2.0", RGB(0, 200, 0), NX_BLACK, FONT_MEDIUM);
    fb_hline(cx + 4, cy + 20, cw - 8, RGB(0, 80, 0));

    // Update
    uint32_t now = timer_get_ticks();
    if (now - u->last_tick >= 8) {
        u->last_tick = now;
        if (u->connected) {
            u->packets_sent += rand() % 5;
            u->packets_recv += rand() % 3;
            u->bandwidth = 50 + rand() % 200;
            u->signal_strength = 60 + rand() % 40;
        }
        // Shift data stream
        for (int i = 39; i > 0; i--) u->data_stream[i] = u->data_stream[i - 1];
        u->data_stream[0] = u->connected ? (rand() % 100) : 0;
    }

    // Status
    char buf[64];
    ksprintf(buf, "Status: %s", u->connected ? "CONNECTED" : "OFFLINE");
    font_draw_string(cx + 8, cy + 28, buf, u->connected ? NX_GREEN : NX_RED, NX_BLACK, FONT_SMALL);

    ksprintf(buf, "Signal: %d%%  BW: %d KB/s", u->signal_strength, u->bandwidth);
    font_draw_string(cx + 8, cy + 42, buf, RGB(0, 180, 0), NX_BLACK, FONT_SMALL);

    ksprintf(buf, "TX: %d pkts  RX: %d pkts", u->packets_sent, u->packets_recv);
    font_draw_string(cx + 8, cy + 56, buf, RGB(0, 150, 0), NX_BLACK, FONT_SMALL);

    // Data stream graph
    font_draw_string(cx + 8, cy + 74, "Data Stream:", RGB(0, 150, 0), NX_BLACK, FONT_SMALL);
    int gx = cx + 8, gy = cy + 88, gw = cw - 16, gh = 60;
    fb_rect(gx, gy, gw, gh, RGB(0, 80, 0));
    for (int i = 0; i < 40 && i < gw - 2; i++) {
        int h = u->data_stream[i] * (gh - 4) / 100;
        int bx = gx + 1 + i * ((gw - 2) / 40);
        fb_fillrect(bx, gy + gh - 2 - h, (gw - 2) / 40 - 1, h, RGB(0, 200 + u->data_stream[i] / 2, 0));
    }

    font_draw_string(cx + 8, cy + 160, "Press C to connect/disconnect", RGB(0, 100, 0), NX_BLACK, FONT_SMALL);
}

static void uplink_key(Window* win, uint8_t key) {
    UplinkState* u = (UplinkState*)win->userdata;
    if (u && (key == 'c' || key == 'C')) u->connected = !u->connected;
}

static void uplink_close(Window* win) { if (win->userdata) kfree(win->userdata); }

extern "C" void app_launch_uplink() {
    Window* w = wm_create_window("GW-Uplink", 130, 90, 350, 190,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE);
    if (!w) return;
    UplinkState* u = (UplinkState*)kmalloc(sizeof(UplinkState));
    memset(u, 0, sizeof(UplinkState));
    u->connected = true;
    u->last_tick = timer_get_ticks();
    w->userdata = u;
    w->on_draw = uplink_draw;
    w->on_key = uplink_key;
    w->on_close = uplink_close;
}

// ============================================================
// GW-STARMAP - Star map viewer
// ============================================================
struct StarMapState {
    int stars[100][3]; // x, y, brightness
    int scroll_x, scroll_y;
    int zoom;
};

static void starmap_draw(Window* win, int cx, int cy, int cw, int ch) {
    StarMapState* s = (StarMapState*)win->userdata;
    if (!s) return;

    fb_fillrect(cx, cy, cw, ch, RGB(5, 5, 20));

    // Draw stars with parallax
    for (int i = 0; i < 100; i++) {
        int sx = (s->stars[i][0] - s->scroll_x * s->stars[i][2] / 256) % cw;
        int sy = (s->stars[i][1] - s->scroll_y * s->stars[i][2] / 256) % ch;
        if (sx < 0) sx += cw;
        if (sy < 0) sy += ch;

        int b = s->stars[i][2];
        uint32_t col = RGB(b, b, b + 50 > 255 ? 255 : b + 50);

        fb_putpixel(cx + sx, cy + sy, col);
        if (b > 180) { // Bright stars are larger
            fb_putpixel(cx + sx + 1, cy + sy, col);
            fb_putpixel(cx + sx, cy + sy + 1, col);
        }
    }

    // Grid
    for (int gx = 0; gx < cw; gx += 50)
        for (int gy = 0; gy < ch; gy += 4)
            fb_putpixel(cx + gx, cy + gy, RGB(20, 20, 60));
    for (int gy = 0; gy < ch; gy += 50)
        for (int gx = 0; gx < cw; gx += 4)
            fb_putpixel(cx + gx, cy + gy, RGB(20, 20, 60));

    // Crosshair
    int chx = cx + cw / 2, chy = cy + ch / 2;
    fb_hline(chx - 10, chy, 20, RGB(0, 100, 200));
    fb_vline(chx, chy - 10, 20, RGB(0, 100, 200));

    // Labels
    font_draw_string(cx + 4, cy + 4, "GW-STARMAP", RGB(100, 150, 255), RGB(5, 5, 20), FONT_SMALL);
    char buf[32];
    ksprintf(buf, "X:%d Y:%d", s->scroll_x, s->scroll_y);
    font_draw_string(cx + 4, cy + ch - 14, buf, RGB(80, 120, 200), RGB(5, 5, 20), FONT_SMALL);
}

static void starmap_key(Window* win, uint8_t key) {
    StarMapState* s = (StarMapState*)win->userdata;
    if (!s) return;
    if (key == KEY_UP) s->scroll_y -= 5;
    if (key == KEY_DOWN) s->scroll_y += 5;
    if (key == KEY_LEFT) s->scroll_x -= 5;
    if (key == KEY_RIGHT) s->scroll_x += 5;
}

static void starmap_close(Window* win) { if (win->userdata) kfree(win->userdata); }

extern "C" void app_launch_starmap() {
    Window* w = wm_create_window("GW-StarMap", 160, 70, 350, 300,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE | WIN_RESIZABLE);
    if (!w) return;
    StarMapState* s = (StarMapState*)kmalloc(sizeof(StarMapState));
    memset(s, 0, sizeof(StarMapState));
    s->zoom = 1;
    for (int i = 0; i < 100; i++) {
        s->stars[i][0] = rand() % 350;
        s->stars[i][1] = rand() % 300;
        s->stars[i][2] = 50 + rand() % 206;
    }
    w->userdata = s;
    w->on_draw = starmap_draw;
    w->on_key = starmap_key;
    w->on_close = starmap_close;
}

// ============================================================
// GW-COMM - Communication terminal
// ============================================================
struct CommState {
    char log[20][50];
    int log_count;
    char input[50];
    int input_len;
    int freq;
};

static void comm_add_log(CommState* c, const char* msg) {
    if (c->log_count >= 20) {
        for (int i = 0; i < 19; i++) memcpy(c->log[i], c->log[i + 1], 50);
        c->log_count = 19;
    }
    strncpy(c->log[c->log_count], msg, 49);
    c->log[c->log_count][49] = 0;
    c->log_count++;
}

static void comm_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)ch;
    CommState* c = (CommState*)win->userdata;
    if (!c) return;

    fb_fillrect(cx, cy, cw, ch, NX_BLACK);
    font_draw_string(cx + 8, cy + 4, "GW-COMM v2.0", RGB(0, 200, 0), NX_BLACK, FONT_MEDIUM);

    char buf[32];
    ksprintf(buf, "FREQ: %d.%d MHz", c->freq / 10, c->freq % 10);
    font_draw_string(cx + cw - 140, cy + 4, buf, RGB(0, 255, 0), NX_BLACK, FONT_SMALL);

    fb_hline(cx + 4, cy + 20, cw - 8, RGB(0, 80, 0));

    // Log
    for (int i = 0; i < c->log_count && i < 12; i++) {
        int y = cy + 24 + i * 10;
        font_draw_string(cx + 8, y, c->log[i], RGB(0, 200, 0), NX_BLACK, FONT_SMALL);
    }

    // Input
    fb_hline(cx + 4, cy + ch - 24, cw - 8, RGB(0, 80, 0));
    char prompt[64];
    ksprintf(prompt, "> %s", c->input);
    font_draw_string(cx + 8, cy + ch - 18, prompt, RGB(0, 255, 0), NX_BLACK, FONT_SMALL);
    if ((timer_get_ticks() / 50) % 2 == 0) {
        int cx2 = cx + 8 + strlen(prompt) * font_char_width(FONT_SMALL);
        font_draw_char(cx2, cy + ch - 18, '_', RGB(0, 255, 0), NX_BLACK, FONT_SMALL);
    }
}

static void comm_key(Window* win, uint8_t key) {
    CommState* c = (CommState*)win->userdata;
    if (!c) return;

    if (key == KEY_ENTER && c->input_len > 0) {
        char msg[64];
        ksprintf(msg, "[YOU] %s", c->input);
        comm_add_log(c, msg);
        // Auto-reply
        const char* replies[] = {
            "[STATION] Signal received.",
            "[STATION] Copy that.",
            "[STATION] Acknowledged.",
            "[STATION] Standing by.",
            "[STATION] Roger, out."
        };
        comm_add_log(c, replies[rand() % 5]);
        c->input[0] = 0; c->input_len = 0;
    } else if (key == KEY_BACKSPACE && c->input_len > 0) {
        c->input[--c->input_len] = 0;
    } else if (key == KEY_UP) { c->freq++; }
    else if (key == KEY_DOWN) { c->freq--; if (c->freq < 0) c->freq = 0; }
    else if (key >= 0x20 && key < 0x7F && c->input_len < 45) {
        c->input[c->input_len++] = (char)key;
        c->input[c->input_len] = 0;
    }
}

static void comm_close(Window* win) { if (win->userdata) kfree(win->userdata); }

extern "C" void app_launch_comm() {
    Window* w = wm_create_window("GW-Comm", 170, 80, 380, 240,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE);
    if (!w) return;
    CommState* c = (CommState*)kmalloc(sizeof(CommState));
    memset(c, 0, sizeof(CommState));
    c->freq = 1215;
    comm_add_log(c, "[SYSTEM] GW-Comm initialized.");
    comm_add_log(c, "[SYSTEM] Listening on 121.5 MHz...");
    w->userdata = c;
    w->on_draw = comm_draw;
    w->on_key = comm_key;
    w->on_close = comm_close;
}

// ============================================================
// GW-PROBE - System probe / diagnostic tool
// ============================================================
static void probe_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)ch;
    fb_fillrect(cx, cy, cw, ch, NX_BLACK);
    font_draw_string(cx + 8, cy + 4, "GW-PROBE v2.0", RGB(255, 200, 0), NX_BLACK, FONT_MEDIUM);
    fb_hline(cx + 4, cy + 20, cw - 8, RGB(80, 60, 0));

    int y = cy + 28;
    uint32_t col = RGB(255, 200, 0);

    font_draw_string(cx + 8, y, "=== SYSTEM PROBE ===", col, NX_BLACK, FONT_SMALL); y += 14;

    char buf[64];
    ksprintf(buf, "CPU:     i686 (x86 Protected Mode)");
    font_draw_string(cx + 8, y, buf, col, NX_BLACK, FONT_SMALL); y += 12;

    ksprintf(buf, "Timer:   PIT @ 100 Hz");
    font_draw_string(cx + 8, y, buf, col, NX_BLACK, FONT_SMALL); y += 12;

    ksprintf(buf, "Ticks:   %d", timer_get_ticks());
    font_draw_string(cx + 8, y, buf, col, NX_BLACK, FONT_SMALL); y += 12;

    ksprintf(buf, "Display: 1024x768 32bpp VESA");
    font_draw_string(cx + 8, y, buf, col, NX_BLACK, FONT_SMALL); y += 12;

    ksprintf(buf, "Memory:  %dKB total, %dKB free",
            pmm_total_page_count() * 4, pmm_free_page_count() * 4);
    font_draw_string(cx + 8, y, buf, col, NX_BLACK, FONT_SMALL); y += 12;

    ksprintf(buf, "Heap:    %d used / %d free", heap_used(), heap_free());
    font_draw_string(cx + 8, y, buf, col, NX_BLACK, FONT_SMALL); y += 12;

    ksprintf(buf, "Windows: %d active", wm_window_count());
    font_draw_string(cx + 8, y, buf, col, NX_BLACK, FONT_SMALL); y += 14;

    font_draw_string(cx + 8, y, "=== I/O PORTS ===", col, NX_BLACK, FONT_SMALL); y += 12;

    // Read some port values
    uint8_t pic1 = inb(0x21);
    uint8_t pic2 = inb(0xA1);
    ksprintf(buf, "PIC1 Mask:  0x%02X", pic1);
    font_draw_string(cx + 8, y, buf, col, NX_BLACK, FONT_SMALL); y += 12;
    ksprintf(buf, "PIC2 Mask:  0x%02X", pic2);
    font_draw_string(cx + 8, y, buf, col, NX_BLACK, FONT_SMALL); y += 12;

    font_draw_string(cx + 8, y, "Probe complete.", RGB(0, 200, 0), NX_BLACK, FONT_SMALL);
}

extern "C" void app_launch_probe() {
    Window* w = wm_create_window("GW-Probe", 140, 70, 350, 300,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE);
    if (!w) return;
    w->on_draw = probe_draw;
}
