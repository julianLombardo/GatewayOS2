#include "apps.h"
#include "../memory/heap.h"

// ============================================================
// SNAKE
// ============================================================
#define SNAKE_GRID 20
#define SNAKE_MAX 400
#define SNAKE_CELL 10

struct SnakeState {
    int body_x[SNAKE_MAX], body_y[SNAKE_MAX];
    int length;
    int dx, dy;
    int food_x, food_y;
    int score;
    bool game_over;
    uint32_t last_move;
};

static void snake_place_food(SnakeState* s) {
    do {
        s->food_x = rand() % SNAKE_GRID;
        s->food_y = rand() % SNAKE_GRID;
        bool on_snake = false;
        for (int i = 0; i < s->length; i++)
            if (s->body_x[i] == s->food_x && s->body_y[i] == s->food_y)
                on_snake = true;
        if (!on_snake) break;
    } while (1);
}

static void snake_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)cw; (void)ch;
    SnakeState* s = (SnakeState*)win->userdata;
    if (!s) return;

    fb_fillrect(cx, cy, cw, ch, NX_LTGRAY);

    int gx = cx + (cw - SNAKE_GRID * SNAKE_CELL) / 2;
    int gy = cy + 20;

    // Game field
    nx_draw_sunken(gx - 2, gy - 2, SNAKE_GRID * SNAKE_CELL + 4, SNAKE_GRID * SNAKE_CELL + 4, NX_WHITE);
    fb_fillrect(gx, gy, SNAKE_GRID * SNAKE_CELL, SNAKE_GRID * SNAKE_CELL, NX_BLACK);

    // Grid dots
    for (int y = 0; y < SNAKE_GRID; y++)
        for (int x = 0; x < SNAKE_GRID; x++)
            fb_putpixel(gx + x * SNAKE_CELL + SNAKE_CELL / 2,
                       gy + y * SNAKE_CELL + SNAKE_CELL / 2, RGB(20, 20, 20));

    // Update logic
    uint32_t now = timer_get_ticks();
    if (!s->game_over && now - s->last_move >= 15) {
        s->last_move = now;
        // Move: shift body
        for (int i = s->length - 1; i > 0; i--) {
            s->body_x[i] = s->body_x[i - 1];
            s->body_y[i] = s->body_y[i - 1];
        }
        s->body_x[0] += s->dx;
        s->body_y[0] += s->dy;

        // Wall collision
        if (s->body_x[0] < 0 || s->body_x[0] >= SNAKE_GRID ||
            s->body_y[0] < 0 || s->body_y[0] >= SNAKE_GRID)
            s->game_over = true;

        // Self collision
        for (int i = 1; i < s->length; i++)
            if (s->body_x[i] == s->body_x[0] && s->body_y[i] == s->body_y[0])
                s->game_over = true;

        // Food
        if (s->body_x[0] == s->food_x && s->body_y[0] == s->food_y) {
            if (s->length < SNAKE_MAX) s->length++;
            s->score += 10;
            snake_place_food(s);
        }
    }

    // Draw food
    fb_fillrect(gx + s->food_x * SNAKE_CELL + 1, gy + s->food_y * SNAKE_CELL + 1,
               SNAKE_CELL - 2, SNAKE_CELL - 2, NX_RED);

    // Draw snake
    for (int i = 0; i < s->length; i++) {
        uint32_t color = (i == 0) ? RGB(0, 200, 0) : NX_GREEN;
        fb_fillrect(gx + s->body_x[i] * SNAKE_CELL + 1,
                   gy + s->body_y[i] * SNAKE_CELL + 1,
                   SNAKE_CELL - 2, SNAKE_CELL - 2, color);
    }

    // Score
    char buf[32];
    ksprintf(buf, "Score: %d", s->score);
    font_draw_string(cx + 8, cy + 4, buf, NX_BLACK, NX_LTGRAY, FONT_SMALL);

    if (s->game_over) {
        font_draw_string(gx + 40, gy + SNAKE_GRID * SNAKE_CELL / 2 - 5,
                        "GAME OVER - Press R", NX_WHITE, NX_BLACK, FONT_SMALL);
    }
}

static void snake_key(Window* win, uint8_t key) {
    SnakeState* s = (SnakeState*)win->userdata;
    if (!s) return;

    if (key == 'r' || key == 'R') {
        s->length = 3; s->dx = 1; s->dy = 0; s->score = 0; s->game_over = false;
        for (int i = 0; i < 3; i++) { s->body_x[i] = 5 - i; s->body_y[i] = 10; }
        snake_place_food(s);
        return;
    }
    if (s->game_over) return;
    if (key == KEY_UP    && s->dy != 1)  { s->dx = 0;  s->dy = -1; }
    if (key == KEY_DOWN  && s->dy != -1) { s->dx = 0;  s->dy = 1;  }
    if (key == KEY_LEFT  && s->dx != 1)  { s->dx = -1; s->dy = 0;  }
    if (key == KEY_RIGHT && s->dx != -1) { s->dx = 1;  s->dy = 0;  }
}

static void snake_close(Window* win) { if (win->userdata) kfree(win->userdata); }

extern "C" void app_launch_snake() {
    Window* w = wm_create_window("Snake", 150, 80, 240, 260,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE);
    if (!w) return;
    SnakeState* s = (SnakeState*)kmalloc(sizeof(SnakeState));
    memset(s, 0, sizeof(SnakeState));
    s->length = 3; s->dx = 1; s->dy = 0;
    for (int i = 0; i < 3; i++) { s->body_x[i] = 5 - i; s->body_y[i] = 10; }
    snake_place_food(s);
    s->last_move = timer_get_ticks();
    w->userdata = s;
    w->on_draw = snake_draw;
    w->on_key = snake_key;
    w->on_close = snake_close;
}

// ============================================================
// PONG
// ============================================================
struct PongState {
    int paddle_y;    // Player paddle (left)
    int cpu_y;       // CPU paddle (right)
    int ball_x, ball_y;
    int ball_dx, ball_dy;
    int score_p, score_c;
    uint32_t last_tick;
};

#define PONG_W 280
#define PONG_H 180
#define PADDLE_H 40
#define PADDLE_W 6
#define BALL_SIZE 4

static void pong_reset_ball(PongState* p) {
    p->ball_x = PONG_W / 2;
    p->ball_y = PONG_H / 2;
    p->ball_dx = (rand() % 2) ? 2 : -2;
    p->ball_dy = (rand() % 2) ? 1 : -1;
}

static void pong_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)cw; (void)ch;
    PongState* p = (PongState*)win->userdata;
    if (!p) return;

    fb_fillrect(cx, cy, cw, ch, NX_LTGRAY);

    int ox = cx + (cw - PONG_W) / 2;
    int oy = cy + 20;

    // Field
    nx_draw_sunken(ox - 2, oy - 2, PONG_W + 4, PONG_H + 4, NX_WHITE);
    fb_fillrect(ox, oy, PONG_W, PONG_H, NX_BLACK);

    // Update
    uint32_t now = timer_get_ticks();
    if (now - p->last_tick >= 3) {
        p->last_tick = now;
        p->ball_x += p->ball_dx;
        p->ball_y += p->ball_dy;

        // Top/bottom bounce
        if (p->ball_y <= 0 || p->ball_y >= PONG_H - BALL_SIZE)
            p->ball_dy = -p->ball_dy;

        // Left paddle hit
        if (p->ball_x <= PADDLE_W + 4 && p->ball_y + BALL_SIZE >= p->paddle_y && p->ball_y <= p->paddle_y + PADDLE_H) {
            p->ball_dx = abs(p->ball_dx);
            p->ball_dy += (p->ball_y - (p->paddle_y + PADDLE_H / 2)) / 8;
        }
        // Right paddle hit
        if (p->ball_x >= PONG_W - PADDLE_W - 4 - BALL_SIZE && p->ball_y + BALL_SIZE >= p->cpu_y && p->ball_y <= p->cpu_y + PADDLE_H) {
            p->ball_dx = -abs(p->ball_dx);
        }

        // Score
        if (p->ball_x < 0) { p->score_c++; pong_reset_ball(p); }
        if (p->ball_x > PONG_W) { p->score_p++; pong_reset_ball(p); }

        // CPU AI
        int cpu_center = p->cpu_y + PADDLE_H / 2;
        if (p->ball_y > cpu_center + 5) p->cpu_y += 2;
        if (p->ball_y < cpu_center - 5) p->cpu_y -= 2;
        if (p->cpu_y < 0) p->cpu_y = 0;
        if (p->cpu_y > PONG_H - PADDLE_H) p->cpu_y = PONG_H - PADDLE_H;
    }

    // Center line
    for (int y = 0; y < PONG_H; y += 8)
        fb_fillrect(ox + PONG_W / 2 - 1, oy + y, 2, 4, NX_DKGRAY);

    // Paddles
    fb_fillrect(ox + 4, oy + p->paddle_y, PADDLE_W, PADDLE_H, NX_WHITE);
    fb_fillrect(ox + PONG_W - 4 - PADDLE_W, oy + p->cpu_y, PADDLE_W, PADDLE_H, NX_WHITE);

    // Ball
    fb_fillrect(ox + p->ball_x, oy + p->ball_y, BALL_SIZE, BALL_SIZE, NX_WHITE);

    // Score
    char buf[32];
    ksprintf(buf, "Player: %d  CPU: %d", p->score_p, p->score_c);
    int tw = strlen(buf) * font_char_width(FONT_SMALL);
    font_draw_string(cx + (cw - tw) / 2, cy + 4, buf, NX_BLACK, NX_LTGRAY, FONT_SMALL);
}

static void pong_key(Window* win, uint8_t key) {
    PongState* p = (PongState*)win->userdata;
    if (!p) return;
    if (key == KEY_UP && p->paddle_y > 0) p->paddle_y -= 8;
    if (key == KEY_DOWN && p->paddle_y < PONG_H - PADDLE_H) p->paddle_y += 8;
}

static void pong_close(Window* win) { if (win->userdata) kfree(win->userdata); }

extern "C" void app_launch_pong() {
    Window* w = wm_create_window("Pong", 200, 100, 320, 230,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE);
    if (!w) return;
    PongState* p = (PongState*)kmalloc(sizeof(PongState));
    memset(p, 0, sizeof(PongState));
    p->paddle_y = PONG_H / 2 - PADDLE_H / 2;
    p->cpu_y = p->paddle_y;
    pong_reset_ball(p);
    p->last_tick = timer_get_ticks();
    w->userdata = p;
    w->on_draw = pong_draw;
    w->on_key = pong_key;
    w->on_close = pong_close;
}

// ============================================================
// MINESWEEPER
// ============================================================
#define MINES_W 10
#define MINES_H 10
#define MINES_COUNT 15
#define MINE_CELL 18

struct MinesState {
    int8_t grid[MINES_H][MINES_W];   // -1 = mine, 0-8 = neighbor count
    bool revealed[MINES_H][MINES_W];
    bool flagged[MINES_H][MINES_W];
    bool game_over;
    bool won;
    int revealed_count;
};

static void mines_count_neighbors(MinesState* m) {
    for (int y = 0; y < MINES_H; y++) {
        for (int x = 0; x < MINES_W; x++) {
            if (m->grid[y][x] == -1) continue;
            int count = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && nx < MINES_W && ny >= 0 && ny < MINES_H && m->grid[ny][nx] == -1)
                        count++;
                }
            m->grid[y][x] = count;
        }
    }
}

static void mines_reveal(MinesState* m, int x, int y) {
    if (x < 0 || x >= MINES_W || y < 0 || y >= MINES_H) return;
    if (m->revealed[y][x] || m->flagged[y][x]) return;
    m->revealed[y][x] = true;
    m->revealed_count++;

    if (m->grid[y][x] == -1) {
        m->game_over = true;
        // Reveal all mines
        for (int j = 0; j < MINES_H; j++)
            for (int i = 0; i < MINES_W; i++)
                if (m->grid[j][i] == -1) m->revealed[j][i] = true;
        return;
    }

    // Flood fill for 0-cells
    if (m->grid[y][x] == 0) {
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
                mines_reveal(m, x + dx, y + dy);
    }

    // Check win
    if (m->revealed_count == MINES_W * MINES_H - MINES_COUNT)
        m->won = true;
}

static void mines_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)cw; (void)ch;
    MinesState* m = (MinesState*)win->userdata;
    if (!m) return;

    fb_fillrect(cx, cy, cw, ch, NX_LTGRAY);

    int ox = cx + (cw - MINES_W * MINE_CELL) / 2;
    int oy = cy + 24;

    // Status
    char buf[32];
    if (m->game_over) strcpy(buf, "GAME OVER! Press R");
    else if (m->won) strcpy(buf, "YOU WIN! Press R");
    else ksprintf(buf, "Mines: %d", MINES_COUNT);
    font_draw_string(cx + 8, cy + 6, buf, NX_BLACK, NX_LTGRAY, FONT_SMALL);

    // Grid
    for (int y = 0; y < MINES_H; y++) {
        for (int x = 0; x < MINES_W; x++) {
            int bx = ox + x * MINE_CELL;
            int by = oy + y * MINE_CELL;

            if (m->revealed[y][x]) {
                nx_draw_sunken(bx, by, MINE_CELL, MINE_CELL, NX_WHITE);
                if (m->grid[y][x] == -1) {
                    // Mine
                    fb_fillrect(bx + 4, by + 4, MINE_CELL - 8, MINE_CELL - 8, NX_RED);
                    fb_putpixel(bx + MINE_CELL / 2, by + MINE_CELL / 2, NX_BLACK);
                } else if (m->grid[y][x] > 0) {
                    char n[2] = {(char)('0' + m->grid[y][x]), 0};
                    uint32_t colors[] = {0, RGB(0,0,200), RGB(0,128,0), RGB(200,0,0),
                                         RGB(0,0,128), RGB(128,0,0), RGB(0,128,128),
                                         NX_BLACK, NX_DKGRAY};
                    font_draw_string(bx + 5, by + 4, n, colors[m->grid[y][x]], NX_WHITE, FONT_SMALL);
                }
            } else if (m->flagged[y][x]) {
                nx_draw_raised(bx, by, MINE_CELL, MINE_CELL);
                font_draw_string(bx + 4, by + 4, "F", NX_RED, NX_LTGRAY, FONT_SMALL);
            } else {
                nx_draw_raised(bx, by, MINE_CELL, MINE_CELL);
            }
        }
    }
}

static void mines_mouse(Window* win, int mx, int my, bool left, bool right) {
    MinesState* m = (MinesState*)win->userdata;
    if (!m || m->game_over || m->won) return;

    int cx = win->x + 1;
    int cy = win->y + 24;
    int cw = win->w - 2;
    int ox = cx + (cw - MINES_W * MINE_CELL) / 2;
    int oy = cy + 24;

    int gx = (mx - ox) / MINE_CELL;
    int gy = (my - oy) / MINE_CELL;
    if (gx < 0 || gx >= MINES_W || gy < 0 || gy >= MINES_H) return;

    if (left) mines_reveal(m, gx, gy);
    if (right) m->flagged[gy][gx] = !m->flagged[gy][gx];
}

static void mines_key(Window* win, uint8_t key) {
    MinesState* m = (MinesState*)win->userdata;
    if (!m) return;
    if (key == 'r' || key == 'R') {
        memset(m, 0, sizeof(MinesState));
        // Place mines
        int placed = 0;
        while (placed < MINES_COUNT) {
            int x = rand() % MINES_W, y = rand() % MINES_H;
            if (m->grid[y][x] != -1) { m->grid[y][x] = -1; placed++; }
        }
        mines_count_neighbors(m);
    }
}

static void mines_close(Window* win) { if (win->userdata) kfree(win->userdata); }

extern "C" void app_launch_mines() {
    Window* w = wm_create_window("Minesweeper", 180, 90, 220, 230,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE);
    if (!w) return;
    MinesState* m = (MinesState*)kmalloc(sizeof(MinesState));
    memset(m, 0, sizeof(MinesState));
    int placed = 0;
    while (placed < MINES_COUNT) {
        int x = rand() % MINES_W, y = rand() % MINES_H;
        if (m->grid[y][x] != -1) { m->grid[y][x] = -1; placed++; }
    }
    mines_count_neighbors(m);
    w->userdata = m;
    w->on_draw = mines_draw;
    w->on_key = mines_key;
    w->on_mouse = mines_mouse;
    w->on_close = mines_close;
}

// ============================================================
// TETRIS
// ============================================================
#define TET_W 10
#define TET_H 20
#define TET_CELL 12

struct TetState {
    uint8_t board[TET_H][TET_W]; // 0=empty, 1-7=color
    int piece_type, piece_rot;
    int piece_x, piece_y;
    int score;
    bool game_over;
    uint32_t last_drop;
    uint32_t drop_interval;
};

// Tetrimino shapes: 4 rotations x 4 cells (x,y offsets)
static const int8_t tetriminos[7][4][4][2] = {
    // I
    {{{0,0},{1,0},{2,0},{3,0}}, {{0,0},{0,1},{0,2},{0,3}}, {{0,0},{1,0},{2,0},{3,0}}, {{0,0},{0,1},{0,2},{0,3}}},
    // O
    {{{0,0},{1,0},{0,1},{1,1}}, {{0,0},{1,0},{0,1},{1,1}}, {{0,0},{1,0},{0,1},{1,1}}, {{0,0},{1,0},{0,1},{1,1}}},
    // T
    {{{0,0},{1,0},{2,0},{1,1}}, {{0,0},{0,1},{0,2},{1,1}}, {{1,0},{0,1},{1,1},{2,1}}, {{1,0},{1,1},{1,2},{0,1}}},
    // S
    {{{1,0},{2,0},{0,1},{1,1}}, {{0,0},{0,1},{1,1},{1,2}}, {{1,0},{2,0},{0,1},{1,1}}, {{0,0},{0,1},{1,1},{1,2}}},
    // Z
    {{{0,0},{1,0},{1,1},{2,1}}, {{1,0},{0,1},{1,1},{0,2}}, {{0,0},{1,0},{1,1},{2,1}}, {{1,0},{0,1},{1,1},{0,2}}},
    // L
    {{{0,0},{0,1},{0,2},{1,2}}, {{0,0},{1,0},{2,0},{0,1}}, {{0,0},{1,0},{1,1},{1,2}}, {{2,0},{0,1},{1,1},{2,1}}},
    // J
    {{{1,0},{1,1},{1,2},{0,2}}, {{0,0},{0,1},{1,1},{2,1}}, {{0,0},{1,0},{0,1},{0,2}}, {{0,0},{1,0},{2,0},{2,1}}},
};

static const uint32_t tet_colors[8] = {
    NX_BLACK, RGB(0,255,255), RGB(255,255,0), RGB(128,0,128),
    RGB(0,255,0), RGB(255,0,0), RGB(255,165,0), RGB(0,0,255)
};

static bool tet_fits(TetState* t, int type, int rot, int px, int py) {
    for (int i = 0; i < 4; i++) {
        int x = px + tetriminos[type][rot][i][0];
        int y = py + tetriminos[type][rot][i][1];
        if (x < 0 || x >= TET_W || y < 0 || y >= TET_H) return false;
        if (t->board[y][x]) return false;
    }
    return true;
}

static void tet_lock(TetState* t) {
    for (int i = 0; i < 4; i++) {
        int x = t->piece_x + tetriminos[t->piece_type][t->piece_rot][i][0];
        int y = t->piece_y + tetriminos[t->piece_type][t->piece_rot][i][1];
        if (y >= 0 && y < TET_H && x >= 0 && x < TET_W)
            t->board[y][x] = t->piece_type + 1;
    }
    // Clear lines
    for (int y = TET_H - 1; y >= 0; y--) {
        bool full = true;
        for (int x = 0; x < TET_W; x++) if (!t->board[y][x]) full = false;
        if (full) {
            for (int yy = y; yy > 0; yy--)
                memcpy(t->board[yy], t->board[yy - 1], TET_W);
            memset(t->board[0], 0, TET_W);
            t->score += 100;
            y++; // Re-check this row
        }
    }
}

static void tet_new_piece(TetState* t) {
    t->piece_type = rand() % 7;
    t->piece_rot = 0;
    t->piece_x = TET_W / 2 - 1;
    t->piece_y = 0;
    if (!tet_fits(t, t->piece_type, t->piece_rot, t->piece_x, t->piece_y))
        t->game_over = true;
}

static void tet_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)cw; (void)ch;
    TetState* t = (TetState*)win->userdata;
    if (!t) return;

    fb_fillrect(cx, cy, cw, ch, NX_LTGRAY);

    int ox = cx + 10;
    int oy = cy + 20;

    // Update
    uint32_t now = timer_get_ticks();
    if (!t->game_over && now - t->last_drop >= t->drop_interval) {
        t->last_drop = now;
        if (tet_fits(t, t->piece_type, t->piece_rot, t->piece_x, t->piece_y + 1)) {
            t->piece_y++;
        } else {
            tet_lock(t);
            tet_new_piece(t);
        }
    }

    // Draw board
    nx_draw_sunken(ox - 2, oy - 2, TET_W * TET_CELL + 4, TET_H * TET_CELL + 4, NX_WHITE);
    for (int y = 0; y < TET_H; y++) {
        for (int x = 0; x < TET_W; x++) {
            int bx = ox + x * TET_CELL;
            int by = oy + y * TET_CELL;
            uint32_t c = tet_colors[t->board[y][x]];
            fb_fillrect(bx, by, TET_CELL - 1, TET_CELL - 1, c);
        }
    }

    // Draw current piece
    if (!t->game_over) {
        for (int i = 0; i < 4; i++) {
            int x = t->piece_x + tetriminos[t->piece_type][t->piece_rot][i][0];
            int y = t->piece_y + tetriminos[t->piece_type][t->piece_rot][i][1];
            if (y >= 0) {
                fb_fillrect(ox + x * TET_CELL, oy + y * TET_CELL,
                           TET_CELL - 1, TET_CELL - 1, tet_colors[t->piece_type + 1]);
            }
        }
    }

    // Score
    char buf[32];
    ksprintf(buf, "Score: %d", t->score);
    font_draw_string(ox + TET_W * TET_CELL + 12, oy + 10, buf, NX_BLACK, NX_LTGRAY, FONT_SMALL);

    if (t->game_over) {
        font_draw_string(ox + TET_W * TET_CELL + 12, oy + 40, "GAME", NX_RED, NX_LTGRAY, FONT_MEDIUM);
        font_draw_string(ox + TET_W * TET_CELL + 12, oy + 58, "OVER", NX_RED, NX_LTGRAY, FONT_MEDIUM);
        font_draw_string(ox + TET_W * TET_CELL + 12, oy + 80, "Press R", NX_BLACK, NX_LTGRAY, FONT_SMALL);
    }
}

static void tet_key(Window* win, uint8_t key) {
    TetState* t = (TetState*)win->userdata;
    if (!t) return;

    if (key == 'r' || key == 'R') {
        memset(t->board, 0, sizeof(t->board));
        t->score = 0; t->game_over = false; t->drop_interval = 40;
        tet_new_piece(t);
        return;
    }
    if (t->game_over) return;

    if (key == KEY_LEFT && tet_fits(t, t->piece_type, t->piece_rot, t->piece_x - 1, t->piece_y))
        t->piece_x--;
    if (key == KEY_RIGHT && tet_fits(t, t->piece_type, t->piece_rot, t->piece_x + 1, t->piece_y))
        t->piece_x++;
    if (key == KEY_DOWN && tet_fits(t, t->piece_type, t->piece_rot, t->piece_x, t->piece_y + 1))
        t->piece_y++;
    if (key == KEY_UP) {
        int new_rot = (t->piece_rot + 1) % 4;
        if (tet_fits(t, t->piece_type, new_rot, t->piece_x, t->piece_y))
            t->piece_rot = new_rot;
    }
    if (key == ' ') { // Hard drop
        while (tet_fits(t, t->piece_type, t->piece_rot, t->piece_x, t->piece_y + 1))
            t->piece_y++;
        tet_lock(t);
        tet_new_piece(t);
    }
}

static void tet_close(Window* win) { if (win->userdata) kfree(win->userdata); }

extern "C" void app_launch_tetris() {
    Window* w = wm_create_window("Tetris", 250, 60, 220, 290,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE);
    if (!w) return;
    TetState* t = (TetState*)kmalloc(sizeof(TetState));
    memset(t, 0, sizeof(TetState));
    t->drop_interval = 40;
    t->last_drop = timer_get_ticks();
    tet_new_piece(t);
    w->userdata = t;
    w->on_draw = tet_draw;
    w->on_key = tet_key;
    w->on_close = tet_close;
}
