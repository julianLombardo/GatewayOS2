#include "apps.h"
#include "../memory/heap.h"

// ============================================================
// CHESS - Chess game with basic AI
// ============================================================
#define CHESS_CELL 28
#define CHESS_EMPTY 0

// Piece encoding: sign=color (+ white, - black), value=type
#define W_PAWN 1
#define W_ROOK 2
#define W_KNIGHT 3
#define W_BISHOP 4
#define W_QUEEN 5
#define W_KING 6

struct ChessState {
    int8_t board[8][8];
    int sel_x, sel_y;   // Selected piece (-1 = none)
    bool white_turn;
    bool game_over;
    char status[40];
};

static void chess_init_board(ChessState* c) {
    memset(c->board, 0, sizeof(c->board));
    // Black pieces (negative)
    int8_t back[] = {-W_ROOK, -W_KNIGHT, -W_BISHOP, -W_QUEEN, -W_KING, -W_BISHOP, -W_KNIGHT, -W_ROOK};
    for (int i = 0; i < 8; i++) {
        c->board[0][i] = back[i];
        c->board[1][i] = -W_PAWN;
        c->board[6][i] = W_PAWN;
    }
    int8_t wback[] = {W_ROOK, W_KNIGHT, W_BISHOP, W_QUEEN, W_KING, W_BISHOP, W_KNIGHT, W_ROOK};
    for (int i = 0; i < 8; i++) c->board[7][i] = wback[i];
    c->sel_x = c->sel_y = -1;
    c->white_turn = true;
    c->game_over = false;
    strcpy(c->status, "White's turn");
}

static void chess_draw_piece(int px, int py, int piece) {
    const char* symbols[] = {"", "P", "R", "N", "B", "Q", "K"};
    int type = piece < 0 ? -piece : piece;
    if (type < 1 || type > 6) return;
    uint32_t col = piece > 0 ? NX_WHITE : NX_BLACK;
    uint32_t outline = piece > 0 ? NX_BLACK : NX_DKGRAY;
    // Draw piece letter centered
    font_draw_string_nobg(px + 8, py + 6, symbols[type], outline, FONT_MEDIUM);
    font_draw_string_nobg(px + 7, py + 5, symbols[type], col, FONT_MEDIUM);
}

static bool chess_valid_move(ChessState* c, int fx, int fy, int tx, int ty) {
    if (tx < 0 || tx >= 8 || ty < 0 || ty >= 8) return false;
    int piece = c->board[fy][fx];
    int target = c->board[ty][tx];
    if (piece == 0) return false;
    // Can't capture own piece
    if (target != 0 && ((piece > 0) == (target > 0))) return false;

    int dx = tx - fx, dy = ty - fy;
    int type = piece < 0 ? -piece : piece;

    switch (type) {
        case W_PAWN: {
            int dir = piece > 0 ? -1 : 1;
            if (dx == 0 && dy == dir && target == 0) return true;
            if (dx == 0 && dy == 2 * dir && fy == (piece > 0 ? 6 : 1) && target == 0 && c->board[fy + dir][fx] == 0) return true;
            if (abs(dx) == 1 && dy == dir && target != 0) return true;
            return false;
        }
        case W_ROOK:
            if (dx != 0 && dy != 0) return false;
            { int sx = dx ? (dx > 0 ? 1 : -1) : 0, sy = dy ? (dy > 0 ? 1 : -1) : 0;
              int cx2 = fx + sx, cy2 = fy + sy;
              while (cx2 != tx || cy2 != ty) { if (c->board[cy2][cx2]) return false; cx2 += sx; cy2 += sy; }
            }
            return true;
        case W_KNIGHT:
            return (abs(dx) == 2 && abs(dy) == 1) || (abs(dx) == 1 && abs(dy) == 2);
        case W_BISHOP:
            if (abs(dx) != abs(dy)) return false;
            { int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1;
              int cx2 = fx + sx, cy2 = fy + sy;
              while (cx2 != tx || cy2 != ty) { if (c->board[cy2][cx2]) return false; cx2 += sx; cy2 += sy; }
            }
            return true;
        case W_QUEEN:
            if (dx != 0 && dy != 0 && abs(dx) != abs(dy)) return false;
            { int sx = dx ? (dx > 0 ? 1 : -1) : 0, sy = dy ? (dy > 0 ? 1 : -1) : 0;
              int cx2 = fx + sx, cy2 = fy + sy;
              while (cx2 != tx || cy2 != ty) { if (c->board[cy2][cx2]) return false; cx2 += sx; cy2 += sy; }
            }
            return true;
        case W_KING:
            return abs(dx) <= 1 && abs(dy) <= 1;
    }
    return false;
}

static void chess_cpu_move(ChessState* c) {
    // Simple AI: try random valid moves
    for (int attempts = 0; attempts < 1000; attempts++) {
        int fx = rand() % 8, fy = rand() % 8;
        if (c->board[fy][fx] >= 0) continue; // Must be black piece
        int tx = rand() % 8, ty = rand() % 8;
        if (chess_valid_move(c, fx, fy, tx, ty)) {
            c->board[ty][tx] = c->board[fy][fx];
            c->board[fy][fx] = 0;
            c->white_turn = true;
            strcpy(c->status, "White's turn");
            return;
        }
    }
    strcpy(c->status, "Black cannot move!");
    c->game_over = true;
}

static void chess_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)cw; (void)ch;
    ChessState* c = (ChessState*)win->userdata;
    if (!c) return;

    fb_fillrect(cx, cy, cw, ch, NX_LTGRAY);

    int ox = cx + (cw - 8 * CHESS_CELL) / 2;
    int oy = cy + 20;

    nx_draw_sunken(ox - 2, oy - 2, 8 * CHESS_CELL + 4, 8 * CHESS_CELL + 4, NX_WHITE);

    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            int bx = ox + x * CHESS_CELL;
            int by = oy + y * CHESS_CELL;
            bool light = (x + y) % 2 == 0;
            uint32_t bg = light ? RGB(240, 220, 180) : RGB(180, 140, 100);
            if (x == c->sel_x && y == c->sel_y) bg = RGB(100, 200, 100);
            fb_fillrect(bx, by, CHESS_CELL, CHESS_CELL, bg);
            if (c->board[y][x] != 0)
                chess_draw_piece(bx, by, c->board[y][x]);
        }
    }

    // Status
    font_draw_string(cx + 8, cy + 4, c->status, NX_BLACK, NX_LTGRAY, FONT_SMALL);
}

static void chess_mouse(Window* win, int mx, int my, bool left, bool right) {
    (void)right;
    ChessState* c = (ChessState*)win->userdata;
    if (!c || !left || c->game_over || !c->white_turn) return;

    int content_x = win->x + 1;
    int content_y = win->y + 24;
    int cw2 = win->w - 2;
    int ox = content_x + (cw2 - 8 * CHESS_CELL) / 2;
    int oy = content_y + 20;

    int gx = (mx - ox) / CHESS_CELL;
    int gy = (my - oy) / CHESS_CELL;
    if (gx < 0 || gx >= 8 || gy < 0 || gy >= 8) return;

    if (c->sel_x < 0) {
        // Select piece
        if (c->board[gy][gx] > 0) { c->sel_x = gx; c->sel_y = gy; }
    } else {
        // Try to move
        if (chess_valid_move(c, c->sel_x, c->sel_y, gx, gy)) {
            // Check if capturing king
            if (c->board[gy][gx] == -W_KING) {
                c->game_over = true;
                strcpy(c->status, "White wins!");
            }
            c->board[gy][gx] = c->board[c->sel_y][c->sel_x];
            c->board[c->sel_y][c->sel_x] = 0;
            c->sel_x = c->sel_y = -1;
            if (!c->game_over) {
                c->white_turn = false;
                strcpy(c->status, "Black thinking...");
                chess_cpu_move(c);
            }
        } else {
            // Deselect or select different
            if (c->board[gy][gx] > 0) { c->sel_x = gx; c->sel_y = gy; }
            else { c->sel_x = c->sel_y = -1; }
        }
    }
}

static void chess_key(Window* win, uint8_t key) {
    ChessState* c = (ChessState*)win->userdata;
    if (c && (key == 'r' || key == 'R')) chess_init_board(c);
}

static void chess_close(Window* win) { if (win->userdata) kfree(win->userdata); }

extern "C" void app_launch_chess() {
    Window* w = wm_create_window("Chess", 180, 60, 280, 280,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE);
    if (!w) return;
    ChessState* c = (ChessState*)kmalloc(sizeof(ChessState));
    chess_init_board(c);
    w->userdata = c;
    w->on_draw = chess_draw;
    w->on_key = chess_key;
    w->on_mouse = chess_mouse;
    w->on_close = chess_close;
}

// ============================================================
// PUZZLE - 15-puzzle sliding tiles
// ============================================================
#define PUZ_SIZE 4
#define PUZ_CELL 50

struct PuzzleState {
    int tiles[PUZ_SIZE][PUZ_SIZE]; // 0 = empty
    int empty_x, empty_y;
    int moves;
    bool won;
};

static void puzzle_shuffle(PuzzleState* p) {
    // Init solved state
    int n = 1;
    for (int y = 0; y < PUZ_SIZE; y++)
        for (int x = 0; x < PUZ_SIZE; x++)
            p->tiles[y][x] = n++;
    p->tiles[PUZ_SIZE - 1][PUZ_SIZE - 1] = 0;
    p->empty_x = PUZ_SIZE - 1;
    p->empty_y = PUZ_SIZE - 1;
    p->moves = 0;
    p->won = false;

    // Random moves to shuffle
    for (int i = 0; i < 200; i++) {
        int dir = rand() % 4;
        int nx = p->empty_x, ny = p->empty_y;
        if (dir == 0) ny--;
        else if (dir == 1) ny++;
        else if (dir == 2) nx--;
        else nx++;
        if (nx >= 0 && nx < PUZ_SIZE && ny >= 0 && ny < PUZ_SIZE) {
            p->tiles[p->empty_y][p->empty_x] = p->tiles[ny][nx];
            p->tiles[ny][nx] = 0;
            p->empty_x = nx;
            p->empty_y = ny;
        }
    }
}

static bool puzzle_check_win(PuzzleState* p) {
    int n = 1;
    for (int y = 0; y < PUZ_SIZE; y++)
        for (int x = 0; x < PUZ_SIZE; x++) {
            if (y == PUZ_SIZE - 1 && x == PUZ_SIZE - 1) continue;
            if (p->tiles[y][x] != n++) return false;
        }
    return true;
}

static void puzzle_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)cw; (void)ch;
    PuzzleState* p = (PuzzleState*)win->userdata;
    if (!p) return;

    fb_fillrect(cx, cy, cw, ch, NX_LTGRAY);

    char buf[32];
    ksprintf(buf, "Moves: %d", p->moves);
    font_draw_string(cx + 8, cy + 4, buf, NX_BLACK, NX_LTGRAY, FONT_SMALL);

    int ox = cx + (cw - PUZ_SIZE * PUZ_CELL) / 2;
    int oy = cy + 22;

    nx_draw_sunken(ox - 2, oy - 2, PUZ_SIZE * PUZ_CELL + 4, PUZ_SIZE * PUZ_CELL + 4, NX_WHITE);

    for (int y = 0; y < PUZ_SIZE; y++) {
        for (int x = 0; x < PUZ_SIZE; x++) {
            int bx = ox + x * PUZ_CELL + 1;
            int by = oy + y * PUZ_CELL + 1;
            int val = p->tiles[y][x];
            if (val == 0) {
                fb_fillrect(bx, by, PUZ_CELL - 2, PUZ_CELL - 2, NX_DKGRAY);
                continue;
            }
            nx_draw_raised_color(bx, by, PUZ_CELL - 2, PUZ_CELL - 2, NX_LTGRAY);
            char num[4];
            ksprintf(num, "%d", val);
            int tw = strlen(num) * font_char_width(FONT_MEDIUM);
            font_draw_string(bx + (PUZ_CELL - 2 - tw) / 2, by + 15, num, NX_BLACK, NX_LTGRAY, FONT_MEDIUM);
        }
    }

    if (p->won)
        font_draw_string(cx + 40, cy + ch - 20, "Solved! Press R to reset", NX_GREEN, NX_LTGRAY, FONT_SMALL);
}

static void puzzle_mouse(Window* win, int mx, int my, bool left, bool right) {
    (void)right;
    PuzzleState* p = (PuzzleState*)win->userdata;
    if (!p || !left || p->won) return;

    int content_x = win->x + 1;
    int content_y = win->y + 24;
    int cw2 = win->w - 2;
    int ox = content_x + (cw2 - PUZ_SIZE * PUZ_CELL) / 2;
    int oy = content_y + 22;

    int gx = (mx - ox) / PUZ_CELL;
    int gy = (my - oy) / PUZ_CELL;
    if (gx < 0 || gx >= PUZ_SIZE || gy < 0 || gy >= PUZ_SIZE) return;

    // Check if adjacent to empty
    if ((abs(gx - p->empty_x) == 1 && gy == p->empty_y) ||
        (abs(gy - p->empty_y) == 1 && gx == p->empty_x)) {
        p->tiles[p->empty_y][p->empty_x] = p->tiles[gy][gx];
        p->tiles[gy][gx] = 0;
        p->empty_x = gx;
        p->empty_y = gy;
        p->moves++;
        if (puzzle_check_win(p)) p->won = true;
    }
}

static void puzzle_key(Window* win, uint8_t key) {
    PuzzleState* p = (PuzzleState*)win->userdata;
    if (p && (key == 'r' || key == 'R')) puzzle_shuffle(p);
}

static void puzzle_close(Window* win) { if (win->userdata) kfree(win->userdata); }

extern "C" void app_launch_puzzle() {
    Window* w = wm_create_window("15 Puzzle", 220, 130, 240, 250,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE);
    if (!w) return;
    PuzzleState* p = (PuzzleState*)kmalloc(sizeof(PuzzleState));
    puzzle_shuffle(p);
    w->userdata = p;
    w->on_draw = puzzle_draw;
    w->on_key = puzzle_key;
    w->on_mouse = puzzle_mouse;
    w->on_close = puzzle_close;
}

// ============================================================
// BILLIARDS - Simple pool/billiards game
// ============================================================
#define POOL_W 340
#define POOL_H 180
#define POOL_BALLS 10

struct Ball {
    int x, y;     // Position * 256 for subpixel
    int vx, vy;   // Velocity * 256
    uint32_t color;
    bool sunk;
};

struct BilliardState {
    Ball balls[POOL_BALLS];
    int aim_angle;
    int power;
    bool aiming;
    int score;
};

static void billiard_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)cw; (void)ch;
    BilliardState* b = (BilliardState*)win->userdata;
    if (!b) return;

    fb_fillrect(cx, cy, cw, ch, NX_LTGRAY);

    int ox = cx + (cw - POOL_W) / 2;
    int oy = cy + 24;

    // Table
    fb_fillrect(ox - 4, oy - 4, POOL_W + 8, POOL_H + 8, RGB(80, 50, 20)); // Rail
    fb_fillrect(ox, oy, POOL_W, POOL_H, RGB(0, 100, 50)); // Felt

    // Pockets (corners + sides)
    int pockets[][2] = {{0,0},{POOL_W/2,0},{POOL_W,0},{0,POOL_H},{POOL_W/2,POOL_H},{POOL_W,POOL_H}};
    for (int i = 0; i < 6; i++) {
        int px = ox + pockets[i][0], py = oy + pockets[i][1];
        for (int dy = -6; dy <= 6; dy++)
            for (int dx = -6; dx <= 6; dx++)
                if (dx * dx + dy * dy <= 36)
                    fb_putpixel(px + dx, py + dy, NX_BLACK);
    }

    // Update physics
    for (int i = 0; i < POOL_BALLS; i++) {
        if (b->balls[i].sunk) continue;
        b->balls[i].x += b->balls[i].vx;
        b->balls[i].y += b->balls[i].vy;
        // Friction
        b->balls[i].vx = b->balls[i].vx * 250 / 256;
        b->balls[i].vy = b->balls[i].vy * 250 / 256;
        if (abs(b->balls[i].vx) < 2) b->balls[i].vx = 0;
        if (abs(b->balls[i].vy) < 2) b->balls[i].vy = 0;

        // Wall bounce
        int px = b->balls[i].x / 256, py = b->balls[i].y / 256;
        if (px < 6) { b->balls[i].x = 6 * 256; b->balls[i].vx = abs(b->balls[i].vx); }
        if (px > POOL_W - 6) { b->balls[i].x = (POOL_W - 6) * 256; b->balls[i].vx = -abs(b->balls[i].vx); }
        if (py < 6) { b->balls[i].y = 6 * 256; b->balls[i].vy = abs(b->balls[i].vy); }
        if (py > POOL_H - 6) { b->balls[i].y = (POOL_H - 6) * 256; b->balls[i].vy = -abs(b->balls[i].vy); }

        // Pocket check
        for (int p = 0; p < 6; p++) {
            int dx = px - pockets[p][0], dy = py - pockets[p][1];
            if (dx * dx + dy * dy < 100) {
                b->balls[i].sunk = true;
                if (i > 0) b->score += 10;
                else { // Cue ball - reset
                    b->balls[0].x = 80 * 256;
                    b->balls[0].y = POOL_H / 2 * 256;
                    b->balls[0].vx = b->balls[0].vy = 0;
                    b->balls[0].sunk = false;
                }
            }
        }
    }

    // Ball collisions
    for (int i = 0; i < POOL_BALLS; i++) {
        if (b->balls[i].sunk) continue;
        for (int j = i + 1; j < POOL_BALLS; j++) {
            if (b->balls[j].sunk) continue;
            int dx = (b->balls[j].x - b->balls[i].x) / 256;
            int dy = (b->balls[j].y - b->balls[i].y) / 256;
            int dist2 = dx * dx + dy * dy;
            if (dist2 < 144 && dist2 > 0) { // 12^2 = 144 (ball diameter)
                // Simple elastic collision
                int tvx = b->balls[i].vx;
                int tvy = b->balls[i].vy;
                b->balls[i].vx = b->balls[j].vx;
                b->balls[i].vy = b->balls[j].vy;
                b->balls[j].vx = tvx;
                b->balls[j].vy = tvy;
                // Separate
                int d = isqrt(dist2);
                if (d == 0) d = 1;
                b->balls[i].x -= dx * 256 / d * 2;
                b->balls[i].y -= dy * 256 / d * 2;
                b->balls[j].x += dx * 256 / d * 2;
                b->balls[j].y += dy * 256 / d * 2;
            }
        }
    }

    // Draw balls
    for (int i = 0; i < POOL_BALLS; i++) {
        if (b->balls[i].sunk) continue;
        int px = ox + b->balls[i].x / 256;
        int py = oy + b->balls[i].y / 256;
        for (int dy = -5; dy <= 5; dy++)
            for (int dx = -5; dx <= 5; dx++)
                if (dx * dx + dy * dy <= 25)
                    fb_putpixel(px + dx, py + dy, b->balls[i].color);
        // Highlight
        fb_putpixel(px - 2, py - 2, NX_WHITE);
    }

    // Aim line from cue ball
    if (!b->balls[0].sunk && b->balls[0].vx == 0 && b->balls[0].vy == 0) {
        int px = ox + b->balls[0].x / 256;
        int py = oy + b->balls[0].y / 256;
        for (int r = 10; r < 40; r += 3) {
            int lx = px + r * cos256(b->aim_angle) / 256;
            int ly = py + r * sin256(b->aim_angle) / 256;
            fb_putpixel(lx, ly, NX_WHITE);
        }
    }

    // Score
    char buf[32];
    ksprintf(buf, "Score: %d", b->score);
    font_draw_string(cx + 8, cy + 4, buf, NX_BLACK, NX_LTGRAY, FONT_SMALL);
    font_draw_string(cx + cw - 160, cy + 4, "Arrows+Space", NX_DKGRAY, NX_LTGRAY, FONT_SMALL);
}

static void billiard_key(Window* win, uint8_t key) {
    BilliardState* b = (BilliardState*)win->userdata;
    if (!b) return;

    if (key == KEY_LEFT) b->aim_angle -= 5;
    if (key == KEY_RIGHT) b->aim_angle += 5;
    if (key == ' ' && !b->balls[0].sunk && b->balls[0].vx == 0 && b->balls[0].vy == 0) {
        b->balls[0].vx = cos256(b->aim_angle) * 8;
        b->balls[0].vy = sin256(b->aim_angle) * 8;
    }
    if (key == 'r' || key == 'R') {
        b->score = 0;
        uint32_t colors[] = {NX_WHITE, RGB(255,0,0), RGB(255,200,0), RGB(0,0,200),
                             RGB(128,0,128), RGB(255,140,0), RGB(0,128,0),
                             RGB(128,0,0), NX_BLACK, RGB(200,200,0)};
        b->balls[0].x = 80 * 256; b->balls[0].y = POOL_H / 2 * 256;
        b->balls[0].vx = b->balls[0].vy = 0; b->balls[0].sunk = false;
        b->balls[0].color = colors[0];
        for (int i = 1; i < POOL_BALLS; i++) {
            b->balls[i].x = (200 + (i % 4) * 14) * 256;
            b->balls[i].y = (POOL_H / 2 - 20 + (i / 4) * 14) * 256;
            b->balls[i].vx = b->balls[i].vy = 0;
            b->balls[i].sunk = false;
            b->balls[i].color = colors[i];
        }
    }
}

static void billiard_close(Window* win) { if (win->userdata) kfree(win->userdata); }

extern "C" void app_launch_billiards() {
    Window* w = wm_create_window("Billiards", 100, 80, 380, 240,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE);
    if (!w) return;
    BilliardState* b = (BilliardState*)kmalloc(sizeof(BilliardState));
    memset(b, 0, sizeof(BilliardState));
    // Init balls
    uint32_t colors[] = {NX_WHITE, RGB(255,0,0), RGB(255,200,0), RGB(0,0,200),
                         RGB(128,0,128), RGB(255,140,0), RGB(0,128,0),
                         RGB(128,0,0), NX_BLACK, RGB(200,200,0)};
    b->balls[0].x = 80 * 256; b->balls[0].y = POOL_H / 2 * 256;
    b->balls[0].color = colors[0];
    for (int i = 1; i < POOL_BALLS; i++) {
        b->balls[i].x = (200 + (i % 4) * 14) * 256;
        b->balls[i].y = (POOL_H / 2 - 20 + (i / 4) * 14) * 256;
        b->balls[i].color = colors[i];
    }
    w->userdata = b;
    w->on_draw = billiard_draw;
    w->on_key = billiard_key;
    w->on_close = billiard_close;
}

// ============================================================
// PAINT - Simple pixel paint program
// ============================================================
#define PAINT_W 300
#define PAINT_H 200

struct PaintState {
    uint32_t canvas[PAINT_W * PAINT_H];
    uint32_t color;
    int brush_size;
    int last_mx, last_my;
    bool drawing;
};

static const uint32_t paint_palette[] = {
    NX_BLACK, NX_WHITE, NX_RED, RGB(0,128,0), RGB(0,0,255),
    RGB(255,255,0), RGB(255,128,0), RGB(128,0,255), RGB(0,255,255),
    RGB(255,0,255), NX_DKGRAY, NX_LTGRAY, RGB(128,64,0), RGB(0,200,0),
    RGB(128,128,255), RGB(255,200,200)
};

static void paint_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)ch;
    PaintState* p = (PaintState*)win->userdata;
    if (!p) return;

    fb_fillrect(cx, cy, cw, ch, NX_LTGRAY);

    // Toolbar
    fb_fillrect(cx, cy, cw, 20, NX_LTGRAY);

    // Palette
    for (int i = 0; i < 16; i++) {
        int px = cx + 4 + i * 14;
        fb_fillrect(px, cy + 2, 12, 14, paint_palette[i]);
        fb_rect(px, cy + 2, 12, 14, NX_BLACK);
        if (paint_palette[i] == p->color)
            fb_rect(px - 1, cy + 1, 14, 16, NX_WHITE);
    }

    // Brush size
    char buf[16];
    ksprintf(buf, "B:%d", p->brush_size);
    font_draw_string(cx + cw - 40, cy + 4, buf, NX_BLACK, NX_LTGRAY, FONT_SMALL);

    // Canvas
    int canvas_x = cx + (cw - PAINT_W) / 2;
    int canvas_y = cy + 22;
    nx_draw_sunken(canvas_x - 2, canvas_y - 2, PAINT_W + 4, PAINT_H + 4, NX_WHITE);

    // Blit canvas to screen
    fb_blit(canvas_x, canvas_y, PAINT_W, PAINT_H, p->canvas, PAINT_W);
}

static void paint_mouse(Window* win, int mx, int my, bool left, bool right) {
    PaintState* p = (PaintState*)win->userdata;
    if (!p) return;

    int content_x = win->x + 1;
    int content_y = win->y + 24;
    int cw2 = win->w - 2;

    // Palette click
    if (left && my >= content_y + 2 && my <= content_y + 16) {
        for (int i = 0; i < 16; i++) {
            int px = content_x + 4 + i * 14;
            if (mx >= px && mx < px + 12) { p->color = paint_palette[i]; return; }
        }
    }

    int canvas_x = content_x + (cw2 - PAINT_W) / 2;
    int canvas_y = content_y + 22;

    if (left || right) {
        int px = mx - canvas_x, py = my - canvas_y;
        if (px >= 0 && px < PAINT_W && py >= 0 && py < PAINT_H) {
            uint32_t c = left ? p->color : NX_WHITE;
            for (int dy = -p->brush_size; dy <= p->brush_size; dy++)
                for (int dx = -p->brush_size; dx <= p->brush_size; dx++) {
                    int nx2 = px + dx, ny2 = py + dy;
                    if (nx2 >= 0 && nx2 < PAINT_W && ny2 >= 0 && ny2 < PAINT_H)
                        if (dx * dx + dy * dy <= p->brush_size * p->brush_size)
                            p->canvas[ny2 * PAINT_W + nx2] = c;
                }
        }
    }
}

static void paint_key(Window* win, uint8_t key) {
    PaintState* p = (PaintState*)win->userdata;
    if (!p) return;
    if (key == '+' || key == '=') { p->brush_size++; if (p->brush_size > 10) p->brush_size = 10; }
    if (key == '-') { p->brush_size--; if (p->brush_size < 1) p->brush_size = 1; }
    if (key == 'c' || key == 'C') {
        for (int i = 0; i < PAINT_W * PAINT_H; i++) p->canvas[i] = NX_WHITE;
    }
}

static void paint_close(Window* win) { if (win->userdata) kfree(win->userdata); }

extern "C" void app_launch_paint() {
    Window* w = wm_create_window("Paint", 80, 60, 340, 250,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE);
    if (!w) return;
    PaintState* p = (PaintState*)kmalloc(sizeof(PaintState));
    memset(p, 0, sizeof(PaintState));
    p->color = NX_BLACK;
    p->brush_size = 2;
    for (int i = 0; i < PAINT_W * PAINT_H; i++) p->canvas[i] = NX_WHITE;
    w->userdata = p;
    w->on_draw = paint_draw;
    w->on_key = paint_key;
    w->on_mouse = paint_mouse;
    w->on_close = paint_close;
}

// ============================================================
// DRAW - Vector drawing tool (basic shapes)
// ============================================================
struct DrawShape {
    int type; // 0=rect, 1=circle, 2=line
    int x1, y1, x2, y2;
    uint32_t color;
};

struct DrawState {
    DrawShape shapes[50];
    int shape_count;
    int current_tool;
    uint32_t current_color;
    int start_x, start_y;
    bool dragging;
};

static void draw_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)ch;
    DrawState* d = (DrawState*)win->userdata;
    if (!d) return;

    fb_fillrect(cx, cy, cw, ch, NX_LTGRAY);

    // Toolbar
    const char* tools[] = {"Rect", "Circle", "Line"};
    for (int i = 0; i < 3; i++) {
        bool sel = (i == d->current_tool);
        nx_draw_raised_color(cx + 4 + i * 55, cy + 2, 50, 16, sel ? NX_DKGRAY : NX_LTGRAY);
        font_draw_string(cx + 10 + i * 55, cy + 5, tools[i],
                        sel ? NX_WHITE : NX_BLACK, sel ? NX_DKGRAY : NX_LTGRAY, FONT_SMALL);
    }

    // Canvas
    int canvas_x = cx + 4, canvas_y = cy + 22;
    int canvas_w = cw - 8, canvas_h = ch - 26;
    nx_draw_sunken(canvas_x, canvas_y, canvas_w, canvas_h, NX_WHITE);
    fb_fillrect(canvas_x + 1, canvas_y + 1, canvas_w - 2, canvas_h - 2, NX_WHITE);

    // Draw shapes
    for (int i = 0; i < d->shape_count; i++) {
        DrawShape* s = &d->shapes[i];
        int sx1 = canvas_x + 1 + s->x1, sy1 = canvas_y + 1 + s->y1;
        int sx2 = canvas_x + 1 + s->x2, sy2 = canvas_y + 1 + s->y2;
        int sw = sx2 - sx1, sh = sy2 - sy1;
        if (s->type == 0) { // Rect
            fb_rect(sx1, sy1, sw, sh, s->color);
        } else if (s->type == 1) { // Circle
            int r = isqrt(sw * sw + sh * sh) / 2;
            int cx2 = (sx1 + sx2) / 2, cy2 = (sy1 + sy2) / 2;
            for (int a = 0; a < 360; a += 2) {
                int px = cx2 + r * cos256(a) / 256;
                int py = cy2 + r * sin256(a) / 256;
                fb_putpixel(px, py, s->color);
            }
        } else if (s->type == 2) { // Line
            int dx = sx2 - sx1, dy = sy2 - sy1;
            int steps = (abs(dx) > abs(dy)) ? abs(dx) : abs(dy);
            if (steps == 0) steps = 1;
            for (int st = 0; st <= steps; st++) {
                int px = sx1 + dx * st / steps;
                int py = sy1 + dy * st / steps;
                fb_putpixel(px, py, s->color);
            }
        }
    }
}

static void draw_mouse(Window* win, int mx, int my, bool left, bool right) {
    (void)right;
    DrawState* d = (DrawState*)win->userdata;
    if (!d) return;

    int content_x = win->x + 1;
    int content_y = win->y + 24;

    // Tool selection
    if (left && my >= content_y + 2 && my <= content_y + 18) {
        for (int i = 0; i < 3; i++) {
            if (mx >= content_x + 4 + i * 55 && mx < content_x + 54 + i * 55) {
                d->current_tool = i;
                return;
            }
        }
    }

    int canvas_x = content_x + 5;
    int canvas_y = content_y + 23;
    int lx = mx - canvas_x, ly = my - canvas_y;

    if (left && !d->dragging) {
        d->dragging = true;
        d->start_x = lx;
        d->start_y = ly;
    } else if (!left && d->dragging) {
        d->dragging = false;
        if (d->shape_count < 50) {
            DrawShape* s = &d->shapes[d->shape_count++];
            s->type = d->current_tool;
            s->x1 = d->start_x; s->y1 = d->start_y;
            s->x2 = lx; s->y2 = ly;
            s->color = d->current_color;
        }
    }
}

static void draw_key(Window* win, uint8_t key) {
    DrawState* d = (DrawState*)win->userdata;
    if (!d) return;
    if (key == 'c' || key == 'C') d->shape_count = 0;
    if (key == '1') d->current_color = NX_BLACK;
    if (key == '2') d->current_color = NX_RED;
    if (key == '3') d->current_color = RGB(0, 0, 255);
    if (key == '4') d->current_color = NX_GREEN;
}

static void draw_close(Window* win) { if (win->userdata) kfree(win->userdata); }

extern "C" void app_launch_draw() {
    Window* w = wm_create_window("Draw", 120, 70, 400, 320,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE | WIN_RESIZABLE);
    if (!w) return;
    DrawState* d = (DrawState*)kmalloc(sizeof(DrawState));
    memset(d, 0, sizeof(DrawState));
    d->current_color = NX_BLACK;
    w->userdata = d;
    w->on_draw = draw_draw;
    w->on_key = draw_key;
    w->on_mouse = draw_mouse;
    w->on_close = draw_close;
}
