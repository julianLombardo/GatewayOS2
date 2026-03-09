#include "apps.h"
#include "../memory/heap.h"
#include "../memory/pmm.h"

#define TERM_COLS 60
#define TERM_ROWS 20
#define TERM_MAX_HIST 100
#define CMD_MAX 128

struct TermState {
    char lines[TERM_MAX_HIST][TERM_COLS + 1];
    int line_count;
    int scroll_top;  // First visible line
    char cmd[CMD_MAX];
    int cmd_len;
    int cursor;
    char cwd[64];
};

static void term_add_line(TermState* t, const char* text) {
    if (t->line_count >= TERM_MAX_HIST) {
        // Shift up
        for (int i = 0; i < TERM_MAX_HIST - 1; i++)
            memcpy(t->lines[i], t->lines[i + 1], TERM_COLS + 1);
        t->line_count = TERM_MAX_HIST - 1;
    }
    strncpy(t->lines[t->line_count], text, TERM_COLS);
    t->lines[t->line_count][TERM_COLS] = 0;
    t->line_count++;
    // Auto-scroll to bottom
    if (t->line_count > TERM_ROWS)
        t->scroll_top = t->line_count - TERM_ROWS;
}

static void term_print(TermState* t, const char* text) {
    // Handle multi-line output by splitting on newlines
    char buf[TERM_COLS + 1];
    int bi = 0;
    for (int i = 0; text[i]; i++) {
        if (text[i] == '\n' || bi >= TERM_COLS) {
            buf[bi] = 0;
            term_add_line(t, buf);
            bi = 0;
        } else {
            buf[bi++] = text[i];
        }
    }
    if (bi > 0) {
        buf[bi] = 0;
        term_add_line(t, buf);
    }
}

static void term_execute(TermState* t) {
    // Show command in output
    char prompt_line[TERM_COLS + 1];
    ksprintf(prompt_line, "%s> %s", t->cwd, t->cmd);
    term_add_line(t, prompt_line);

    if (t->cmd_len == 0) return;

    // Parse command
    char cmd[CMD_MAX];
    strncpy(cmd, t->cmd, CMD_MAX);

    // Get first word
    char* arg = NULL;
    for (int i = 0; cmd[i]; i++) {
        if (cmd[i] == ' ') {
            cmd[i] = 0;
            arg = cmd + i + 1;
            while (*arg == ' ') arg++;
            break;
        }
    }

    if (strcmp(cmd, "help") == 0) {
        term_print(t, "Gateway OS2 Terminal v2.0");
        term_print(t, "Commands:");
        term_print(t, "  help       - Show this help");
        term_print(t, "  clear      - Clear screen");
        term_print(t, "  echo <msg> - Print message");
        term_print(t, "  uname      - System info");
        term_print(t, "  uptime     - System uptime");
        term_print(t, "  mem        - Memory usage");
        term_print(t, "  date       - Current tick count");
        term_print(t, "  calc <exp> - Simple math (e.g. calc 5+3)");
        term_print(t, "  rand       - Random number");
        term_print(t, "  cowsay <m> - ASCII cow says message");
        term_print(t, "  matrix     - Matrix effect demo");
        term_print(t, "  version    - OS version");
        term_print(t, "  whoami     - Current user");
        term_print(t, "  hostname   - System hostname");
        term_print(t, "  ps         - List running windows");
    } else if (strcmp(cmd, "clear") == 0) {
        t->line_count = 0;
        t->scroll_top = 0;
    } else if (strcmp(cmd, "echo") == 0) {
        term_print(t, arg ? arg : "");
    } else if (strcmp(cmd, "uname") == 0) {
        term_print(t, "GatewayOS2 v2.0 i686 (x86 Protected Mode)");
    } else if (strcmp(cmd, "version") == 0) {
        term_print(t, "The Gateway OS2 - Version 2.0");
        term_print(t, "Built with i686-elf-g++ (GCC 15.2)");
    } else if (strcmp(cmd, "whoami") == 0) {
        term_print(t, "admin");
    } else if (strcmp(cmd, "hostname") == 0) {
        term_print(t, "gateway");
    } else if (strcmp(cmd, "uptime") == 0) {
        uint32_t ticks = timer_get_ticks();
        uint32_t secs = ticks / 100;
        uint32_t mins = secs / 60;
        uint32_t hrs = mins / 60;
        char buf[64];
        ksprintf(buf, "Up %d:%02d:%02d (%d ticks)", hrs, mins % 60, secs % 60, ticks);
        term_print(t, buf);
    } else if (strcmp(cmd, "mem") == 0) {
        uint32_t free_pages = pmm_free_page_count();
        uint32_t total_pages = pmm_total_page_count();
        char buf[64];
        ksprintf(buf, "Physical: %d/%d pages free (%dKB/%dKB)",
                free_pages, total_pages, free_pages * 4, total_pages * 4);
        term_print(t, buf);
        ksprintf(buf, "Heap: %d bytes used, %d bytes free",
                heap_used(), heap_free());
        term_print(t, buf);
    } else if (strcmp(cmd, "date") == 0) {
        char buf[64];
        ksprintf(buf, "System ticks: %d", timer_get_ticks());
        term_print(t, buf);
    } else if (strcmp(cmd, "rand") == 0) {
        char buf[32];
        ksprintf(buf, "%d", rand() % 1000000);
        term_print(t, buf);
    } else if (strcmp(cmd, "calc") == 0) {
        if (!arg) { term_print(t, "Usage: calc <num><op><num>"); return; }
        // Simple parser: num op num
        int a = 0, b = 0;
        char op = 0;
        int i = 0;
        bool neg_a = false;
        if (arg[i] == '-') { neg_a = true; i++; }
        while (arg[i] >= '0' && arg[i] <= '9') { a = a * 10 + (arg[i] - '0'); i++; }
        if (neg_a) a = -a;
        op = arg[i++];
        bool neg_b = false;
        if (arg[i] == '-') { neg_b = true; i++; }
        while (arg[i] >= '0' && arg[i] <= '9') { b = b * 10 + (arg[i] - '0'); i++; }
        if (neg_b) b = -b;

        int result = 0;
        bool ok = true;
        switch (op) {
            case '+': result = a + b; break;
            case '-': result = a - b; break;
            case '*': result = a * b; break;
            case '/': if (b != 0) result = a / b; else { term_print(t, "Division by zero"); ok = false; } break;
            case '%': if (b != 0) result = a % b; else { term_print(t, "Division by zero"); ok = false; } break;
            default: term_print(t, "Unknown operator"); ok = false;
        }
        if (ok) {
            char buf[64];
            ksprintf(buf, "= %d", result);
            term_print(t, buf);
        }
    } else if (strcmp(cmd, "cowsay") == 0) {
        const char* msg = arg ? arg : "Moo!";
        int len = strlen(msg);
        if (len > 40) len = 40;
        char border[64];
        memset(border, '-', len + 2);
        border[len + 2] = 0;
        char line[64];
        ksprintf(line, " %s ", border);
        term_print(t, line);
        ksprintf(line, "< %s >", msg);
        term_print(t, line);
        ksprintf(line, " %s ", border);
        term_print(t, line);
        term_print(t, "        \\   ^__^");
        term_print(t, "         \\  (oo)\\_______");
        term_print(t, "            (__)\\       )\\/\\");
        term_print(t, "                ||----w |");
        term_print(t, "                ||     ||");
    } else if (strcmp(cmd, "ps") == 0) {
        term_print(t, "PID  TITLE");
        term_print(t, "---  -----");
        for (int i = 0; i < wm_window_count(); i++) {
            Window* w = wm_get_window(i);
            if (w) {
                char buf[64];
                ksprintf(buf, "%3d  %s", w->id, w->title);
                term_print(t, buf);
            }
        }
    } else {
        char buf[TERM_COLS + 1];
        ksprintf(buf, "%s: command not found", cmd);
        term_print(t, buf);
    }

    t->cmd[0] = 0;
    t->cmd_len = 0;
    t->cursor = 0;
}

static void term_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)cw; (void)ch;
    TermState* t = (TermState*)win->userdata;
    if (!t) return;

    // Black background
    fb_fillrect(cx, cy, cw, ch, NX_BLACK);

    // Draw visible lines
    int y = cy + 2;
    int font_h = 10; // FONT_SMALL height + spacing
    for (int i = 0; i < TERM_ROWS && (t->scroll_top + i) < t->line_count; i++) {
        font_draw_string(cx + 4, y, t->lines[t->scroll_top + i],
                        RGB(0, 255, 0), NX_BLACK, FONT_SMALL);
        y += font_h;
    }

    // Draw prompt + command line
    y = cy + 2 + TERM_ROWS * font_h;
    char prompt[TERM_COLS + 1];
    ksprintf(prompt, "%s> %s", t->cwd, t->cmd);
    font_draw_string(cx + 4, y, prompt, RGB(0, 255, 0), NX_BLACK, FONT_SMALL);

    // Cursor
    int cx_pos = cx + 4 + strlen(prompt) * font_char_width(FONT_SMALL);
    if ((timer_get_ticks() / 50) % 2 == 0)
        font_draw_char(cx_pos, y, '_', RGB(0, 255, 0), NX_BLACK, FONT_SMALL);
}

static void term_key(Window* win, uint8_t key) {
    TermState* t = (TermState*)win->userdata;
    if (!t) return;

    if (key == KEY_ENTER) {
        term_execute(t);
    } else if (key == KEY_BACKSPACE) {
        if (t->cmd_len > 0) {
            t->cmd[--t->cmd_len] = 0;
        }
    } else if (key == KEY_PGUP) {
        t->scroll_top -= TERM_ROWS / 2;
        if (t->scroll_top < 0) t->scroll_top = 0;
    } else if (key == KEY_PGDN) {
        t->scroll_top += TERM_ROWS / 2;
        if (t->scroll_top > t->line_count - TERM_ROWS)
            t->scroll_top = t->line_count - TERM_ROWS;
        if (t->scroll_top < 0) t->scroll_top = 0;
    } else if (key >= 0x20 && key < 0x7F && t->cmd_len < CMD_MAX - 1) {
        t->cmd[t->cmd_len++] = (char)key;
        t->cmd[t->cmd_len] = 0;
    }
}

static void term_close(Window* win) {
    if (win->userdata) kfree(win->userdata);
}

extern "C" void app_launch_terminal() {
    Window* w = wm_create_window("Terminal", 100, 60, 500, 240,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE | WIN_RESIZABLE);
    if (!w) return;

    TermState* t = (TermState*)kmalloc(sizeof(TermState));
    memset(t, 0, sizeof(TermState));
    strcpy(t->cwd, "/");

    term_print(t, "Gateway OS2 Terminal v2.0");
    term_print(t, "Type 'help' for commands.");
    term_print(t, "");

    w->userdata = t;
    w->on_draw = term_draw;
    w->on_key = term_key;
    w->on_close = term_close;
}
