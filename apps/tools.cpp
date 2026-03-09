#include "apps.h"
#include "../memory/heap.h"
#include "../memory/pmm.h"
#include "../drivers/ports.h"
#include "../drivers/e1000.h"
#include "../net/net.h"

// ============================================================
// ABOUT
// ============================================================
static void about_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)win;
    // Dark gradient background
    for (int yy = 0; yy < ch; yy++) {
        uint8_t r = 35 - (yy * 15 / ch);
        uint8_t g = 40 - (yy * 15 / ch);
        uint8_t b = 60 - (yy * 20 / ch);
        fb_hline(cx, cy + yy, cw, RGB(r, g, b));
    }

    // Gateway arch logo (larger, amber colored)
    int lx = cx + cw / 2 - 35;
    int ly = cy + 12;
    uint32_t arch_col = NX_AMBER;
    for (int a = 0; a <= 180; a++) {
        for (int t = 0; t < 3; t++) {
            int x = lx + 35 + (30 - t) * cos256(a) / 256;
            int y = ly + 35 - (30 - t) * sin256(a) / 256;
            fb_putpixel(x, y, arch_col);
        }
    }
    // Pillars
    for (int t = 0; t < 3; t++) {
        fb_vline(lx + 5 + t, ly + 35, 18, arch_col);
        fb_vline(lx + 65 - t, ly + 35, 18, arch_col);
    }
    // Base
    fb_hline(lx + 2, ly + 53, 67, arch_col);

    int y = cy + 72;
    int center_w;
    uint32_t bg = RGB(30, 35, 50);

    const char* t1 = "Gateway OS2";
    center_w = strlen(t1) * font_char_width(FONT_LARGE);
    font_draw_string(cx + (cw - center_w) / 2, y, t1, NX_AMBER, bg, FONT_LARGE);
    y += 20;

    const char* t2 = "Version 1.0";
    center_w = strlen(t2) * font_char_width(FONT_MEDIUM);
    font_draw_string(cx + (cw - center_w) / 2, y, t2, NX_WHITE, bg, FONT_MEDIUM);
    y += 18;

    // Separator line in amber
    fb_hline(cx + 30, y, cw - 60, RGB(100, 80, 40));
    fb_hline(cx + 30, y + 1, cw - 60, RGB(60, 50, 30));
    y += 10;

    font_draw_string(cx + 24, y, "x86 Protected Mode Kernel", NX_LTGRAY, bg, FONT_SMALL);
    y += 13;
    font_draw_string(cx + 24, y, "NeXTSTEP-inspired Desktop", NX_LTGRAY, bg, FONT_SMALL);
    y += 13;
    font_draw_string(cx + 24, y, "1024x768x32 VESA | PE32 Loader", NX_LTGRAY, bg, FONT_SMALL);
    y += 13;
    font_draw_string(cx + 24, y, "50 Applications | Win32 Shim", NX_LTGRAY, bg, FONT_SMALL);
    y += 13;

    char buf[64];
    ksprintf(buf, "Memory: %d KB | Heap: %d KB", pmm_total_page_count() * 4, heap_get_total() / 1024);
    font_draw_string(cx + 24, y, buf, RGB(120, 120, 150), bg, FONT_SMALL);
    y += 18;

    const char* quote = "\"Many are called, Few Are Chosen.\"";
    center_w = strlen(quote) * font_char_width(FONT_SMALL);
    font_draw_string(cx + (cw - center_w) / 2, y, quote, RGB(180, 150, 80), bg, FONT_SMALL);
}

extern "C" void app_launch_about() {
    Window* w = wm_create_window("About Gateway OS2", 250, 130, 300, 260,
                                  WIN_CLOSEABLE);
    if (!w) return;
    w->on_draw = about_draw;
}

// ============================================================
// HELP VIEWER
// ============================================================
struct HelpState {
    int scroll;
};

static const char* help_lines[] = {
    "Welcome to Gateway OS2!",
    "",
    "KEYBOARD SHORTCUTS:",
    "  Alt+Tab    - Switch windows",
    "  Alt+F4     - Close window",
    "  Arrow keys - Navigate",
    "",
    "MOUSE:",
    "  Click title bar - Focus & drag",
    "  Drag corner     - Resize window",
    "  Click close btn - Close window",
    "  Click dock      - Launch app",
    "  Right-click     - Context menu",
    "",
    "MENUBAR (top of screen):",
    "  System      - Terminal, Prefs",
    "  Productivity- Edit, Mail, Notes,",
    "                Calc, Clock, Calendar",
    "  Creative    - Draw, Paint, Color,",
    "                Font Viewer, Music",
    "  Games       - Chess, Snake, Pong,",
    "                Tetris, Mines, etc.",
    "  Sci-Fi      - Decrypt, Radar,",
    "                Neural, Uplink, etc.",
    "  Tools       - SysMon, TaskMgr,",
    "                HexView, Log, etc.",
    "  Intel       - Cipher, Fortress,",
    "                Sentinel, NetScan",
    "",
    "WORKSPACE:",
    "  Click folders to select, click",
    "  again to open. Backspace to go",
    "  back. Works in Mail too!",
    "",
    "TERMINAL COMMANDS:",
    "  Type 'help' in Terminal for a",
    "  list of available commands.",
    "",
    "47 applications available!",
    NULL
};

static void help_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)cw;
    HelpState* h = (HelpState*)win->userdata;
    fb_fillrect(cx, cy, cw, ch, NX_WHITE);

    int y = cy + 4;
    int max_lines = (ch - 8) / 12;
    int start = h ? h->scroll : 0;

    for (int i = start; help_lines[i] && i < start + max_lines; i++) {
        uint32_t col = (i == 0) ? NX_BLACK : ((help_lines[i][0] == ' ') ? NX_DKGRAY : NX_BLACK);
        int size = (i == 0) ? FONT_MEDIUM : FONT_SMALL;
        font_draw_string(cx + 8, y, help_lines[i], col, NX_WHITE, size);
        y += (i == 0) ? 16 : 12;
    }
}

static void help_key(Window* win, uint8_t key) {
    HelpState* h = (HelpState*)win->userdata;
    if (!h) return;
    if (key == KEY_UP && h->scroll > 0) h->scroll--;
    if (key == KEY_DOWN) h->scroll++;
    if (key == KEY_PGUP) { h->scroll -= 10; if (h->scroll < 0) h->scroll = 0; }
    if (key == KEY_PGDN) h->scroll += 10;
}

static void help_close(Window* win) { if (win->userdata) kfree(win->userdata); }

extern "C" void app_launch_help() {
    Window* w = wm_create_window("Help Viewer", 80, 50, 300, 350,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE | WIN_RESIZABLE);
    if (!w) return;
    HelpState* h = (HelpState*)kmalloc(sizeof(HelpState));
    memset(h, 0, sizeof(HelpState));
    w->userdata = h;
    w->on_draw = help_draw;
    w->on_key = help_key;
    w->on_close = help_close;
}

// ============================================================
// PREFERENCES
// ============================================================
static void prefs_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)ch;
    fb_fillrect(cx, cy, cw, ch, NX_LTGRAY);

    font_draw_string(cx + 8, cy + 8, "System Preferences", NX_BLACK, NX_LTGRAY, FONT_MEDIUM);
    nx_draw_separator(cx + 4, cy + 26, cw - 8);

    int y = cy + 36;
    // Display section
    font_draw_string(cx + 8, y, "Display", NX_BLACK, NX_LTGRAY, FONT_MEDIUM); y += 18;
    font_draw_string(cx + 20, y, "Resolution: 1024 x 768", NX_DKGRAY, NX_LTGRAY, FONT_SMALL); y += 14;
    font_draw_string(cx + 20, y, "Color Depth: 32-bit", NX_DKGRAY, NX_LTGRAY, FONT_SMALL); y += 14;
    font_draw_string(cx + 20, y, "Theme: NeXTSTEP Classic", NX_DKGRAY, NX_LTGRAY, FONT_SMALL); y += 20;

    // Keyboard
    font_draw_string(cx + 8, y, "Keyboard", NX_BLACK, NX_LTGRAY, FONT_MEDIUM); y += 18;
    font_draw_string(cx + 20, y, "Layout: US QWERTY", NX_DKGRAY, NX_LTGRAY, FONT_SMALL); y += 14;
    font_draw_string(cx + 20, y, "Repeat Rate: Normal", NX_DKGRAY, NX_LTGRAY, FONT_SMALL); y += 20;

    // Sound
    font_draw_string(cx + 8, y, "Sound", NX_BLACK, NX_LTGRAY, FONT_MEDIUM); y += 18;
    font_draw_string(cx + 20, y, "Output: PC Speaker", NX_DKGRAY, NX_LTGRAY, FONT_SMALL);
}

extern "C" void app_launch_preferences() {
    Window* w = wm_create_window("Preferences", 200, 120, 300, 260,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE);
    if (!w) return;
    w->on_draw = prefs_draw;
}

// ============================================================
// HEX VIEWER
// ============================================================
struct HexState {
    uint32_t address;
    int bytes_per_line;
};

static void hex_draw(Window* win, int cx, int cy, int cw, int ch) {
    HexState* h = (HexState*)win->userdata;
    if (!h) return;

    fb_fillrect(cx, cy, cw, ch, NX_BLACK);

    font_draw_string(cx + 4, cy + 2, "Hex Viewer", RGB(0, 200, 0), NX_BLACK, FONT_SMALL);
    char addr_buf[32];
    ksprintf(addr_buf, "Addr: 0x%08X", h->address);
    font_draw_string(cx + cw - 160, cy + 2, addr_buf, RGB(0, 200, 0), NX_BLACK, FONT_SMALL);
    fb_hline(cx + 2, cy + 14, cw - 4, RGB(0, 80, 0));

    int rows = (ch - 18) / 10;
    int y = cy + 18;

    for (int r = 0; r < rows; r++) {
        uint32_t addr = h->address + r * 16;
        char line[80];
        int pos = 0;

        // Address
        pos += ksprintf(line + pos, "%08X  ", addr);

        // Hex bytes
        volatile uint8_t* ptr = (volatile uint8_t*)addr;
        for (int i = 0; i < 16; i++) {
            pos += ksprintf(line + pos, "%02X ", ptr[i]);
            if (i == 7) line[pos++] = ' ';
        }
        line[pos++] = ' ';

        // ASCII
        for (int i = 0; i < 16; i++) {
            uint8_t b = ptr[i];
            line[pos++] = (b >= 0x20 && b < 0x7F) ? b : '.';
        }
        line[pos] = 0;

        font_draw_string(cx + 4, y, line, RGB(0, 200, 0), NX_BLACK, FONT_SMALL);
        y += 10;
    }
}

static void hex_key(Window* win, uint8_t key) {
    HexState* h = (HexState*)win->userdata;
    if (!h) return;
    if (key == KEY_UP) h->address -= 16;
    if (key == KEY_DOWN) h->address += 16;
    if (key == KEY_PGUP) h->address -= 256;
    if (key == KEY_PGDN) h->address += 256;
    if (key == KEY_HOME) h->address = 0x100000; // Kernel start
}

static void hex_close(Window* win) { if (win->userdata) kfree(win->userdata); }

extern "C" void app_launch_hexview() {
    Window* w = wm_create_window("Hex Viewer", 120, 80, 520, 300,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE | WIN_RESIZABLE);
    if (!w) return;
    HexState* h = (HexState*)kmalloc(sizeof(HexState));
    memset(h, 0, sizeof(HexState));
    h->address = 0x100000; // Start at 1MB (kernel load addr)
    h->bytes_per_line = 16;
    w->userdata = h;
    w->on_draw = hex_draw;
    w->on_key = hex_key;
    w->on_close = hex_close;
}

// ============================================================
// NETWORK INFO
// ============================================================
static void netinfo_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)ch;
    fb_fillrect(cx, cy, cw, ch, NX_LTGRAY);

    font_draw_string(cx + 8, cy + 8, "Network Information", NX_BLACK, NX_LTGRAY, FONT_MEDIUM);
    nx_draw_separator(cx + 4, cy + 26, cw - 8);

    int y = cy + 36;
    char buf[64];
    NetConfig* nc = net_get_config();
    bool up = net_is_up();

    ksprintf(buf, "Status: %s", up ? "Connected" : "Disconnected");
    font_draw_string(cx + 8, y, buf, up ? NX_GREEN : NX_RED, NX_LTGRAY, FONT_SMALL); y += 16;

    font_draw_string(cx + 8, y, "Interface: eth0 (E1000)", NX_BLACK, NX_LTGRAY, FONT_SMALL); y += 14;

    // MAC address
    uint8_t mac[6];
    e1000_get_mac(mac);
    ksprintf(buf, "MAC:  %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    font_draw_string(cx + 20, y, buf, NX_DKGRAY, NX_LTGRAY, FONT_SMALL); y += 14;

    if (nc && nc->configured) {
        ksprintf(buf, "IP:   %d.%d.%d.%d",
            nc->ip & 0xFF, (nc->ip >> 8) & 0xFF, (nc->ip >> 16) & 0xFF, (nc->ip >> 24) & 0xFF);
        font_draw_string(cx + 20, y, buf, NX_DKGRAY, NX_LTGRAY, FONT_SMALL); y += 12;
        ksprintf(buf, "Mask: %d.%d.%d.%d",
            nc->subnet & 0xFF, (nc->subnet >> 8) & 0xFF, (nc->subnet >> 16) & 0xFF, (nc->subnet >> 24) & 0xFF);
        font_draw_string(cx + 20, y, buf, NX_DKGRAY, NX_LTGRAY, FONT_SMALL); y += 12;
        ksprintf(buf, "GW:   %d.%d.%d.%d",
            nc->gateway & 0xFF, (nc->gateway >> 8) & 0xFF, (nc->gateway >> 16) & 0xFF, (nc->gateway >> 24) & 0xFF);
        font_draw_string(cx + 20, y, buf, NX_DKGRAY, NX_LTGRAY, FONT_SMALL); y += 14;
        ksprintf(buf, "DNS:  %d.%d.%d.%d",
            nc->dns & 0xFF, (nc->dns >> 8) & 0xFF, (nc->dns >> 16) & 0xFF, (nc->dns >> 24) & 0xFF);
        font_draw_string(cx + 20, y, buf, NX_DKGRAY, NX_LTGRAY, FONT_SMALL); y += 16;
    } else {
        font_draw_string(cx + 20, y, "IP:   Not configured (DHCP)", NX_DKGRAY, NX_LTGRAY, FONT_SMALL); y += 12;
        font_draw_string(cx + 20, y, "Mask: --", NX_DKGRAY, NX_LTGRAY, FONT_SMALL); y += 12;
        font_draw_string(cx + 20, y, "GW:   --", NX_DKGRAY, NX_LTGRAY, FONT_SMALL); y += 14;
        font_draw_string(cx + 20, y, "DNS:  --", NX_DKGRAY, NX_LTGRAY, FONT_SMALL); y += 16;
    }

    font_draw_string(cx + 8, y, "Link: E1000 (Intel 82540EM)", NX_DKGRAY, NX_LTGRAY, FONT_SMALL); y += 12;
    ksprintf(buf, "Link Up: %s", e1000_link_up() ? "Yes" : "No");
    font_draw_string(cx + 8, y, buf, NX_DKGRAY, NX_LTGRAY, FONT_SMALL);
}

extern "C" void app_launch_netinfo() {
    Window* w = wm_create_window("Network Info", 200, 120, 300, 200,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE);
    if (!w) return;
    w->on_draw = netinfo_draw;
}

// ============================================================
// WORKSPACE MANAGER - File browser with clickable folders
// ============================================================
struct WorkspaceState {
    int selected;   // -1 = none
    int depth;      // 0 = root, 1 = inside folder
    int folder;     // which root folder we entered
};

static const char* ws_root_items[] = {"Apps", "Documents", "System", "tmp", "Desktop", "lib"};
static const int ws_root_count = 6;

static const char* ws_sub_items[][6] = {
    {"Terminal", "Calculator", "Edit", "Clock", "Chess", "Paint"},    // Apps
    {"readme.txt", "notes.txt", "todo.txt", "", "", ""},              // Documents
    {"kernel", "drivers", "gui", "memory", "boot", ""},               // System
    {"cache", "logs", "", "", "", ""},                                 // tmp
    {"wallpaper", "", "", "", "", ""},                                 // Desktop
    {"libc", "libmath", "libgui", "", "", ""},                        // lib
};
static const int ws_sub_counts[] = {6, 3, 5, 2, 1, 3};

static void workspace_draw(Window* win, int cx, int cy, int cw, int ch) {
    WorkspaceState* ws = (WorkspaceState*)win->userdata;
    fb_fillrect(cx, cy, cw, ch, NX_WHITE);

    // Toolbar
    fb_fillrect(cx, cy, cw, 24, NX_LTGRAY);
    if (ws && ws->depth > 0) {
        nx_draw_button(cx + 4, cy + 2, 40, 18, "Back", false, false);
    }
    nx_draw_separator(cx, cy + 24, cw);

    // Path bar
    nx_draw_sunken(cx + 48, cy + 2, cw - 52, 18, NX_WHITE);
    if (ws && ws->depth > 0) {
        char path[40];
        ksprintf(path, "/home/admin/%s", ws_root_items[ws->folder]);
        font_draw_string(cx + 52, cy + 5, path, NX_BLACK, NX_WHITE, FONT_SMALL);
    } else {
        font_draw_string(cx + 52, cy + 5, "/home/admin", NX_BLACK, NX_WHITE, FONT_SMALL);
    }

    int y = cy + 30;
    int icon_w = 70, icon_h = 60;

    const char** items;
    int count;
    if (ws && ws->depth > 0) {
        items = ws_sub_items[ws->folder];
        count = ws_sub_counts[ws->folder];
    } else {
        items = ws_root_items;
        count = ws_root_count;
    }

    for (int i = 0; i < count; i++) {
        if (items[i][0] == 0) continue;
        int ix = cx + 10 + (i % 5) * (icon_w + 10);
        int iy = y + (i / 5) * (icon_h + 10);

        bool is_sel = ws && (ws->selected == i);

        // Selection highlight
        if (is_sel) {
            fb_fillrect(ix + 4, iy - 2, icon_w - 8, icon_h + 4, RGB(200, 210, 240));
        }

        // Determine icon type: folder vs file
        bool is_folder = (ws == NULL || ws->depth == 0);
        // In subdir, check if it looks like a file (has a dot)
        if (ws && ws->depth > 0) {
            is_folder = true;
            for (int c = 0; items[i][c]; c++) {
                if (items[i][c] == '.') { is_folder = false; break; }
            }
        }

        if (is_folder) {
            // Folder icon - tab on top
            fb_fillrect(ix + 15, iy, 20, 5, RGB(255, 200, 80));
            fb_fillrect(ix + 10, iy + 5, 50, 30, RGB(255, 220, 100));
            fb_rect(ix + 10, iy + 5, 50, 30, RGB(200, 170, 60));
            // Highlight edge
            fb_hline(ix + 11, iy + 6, 48, RGB(255, 240, 180));
        } else {
            // File icon - white page with corner fold
            fb_fillrect(ix + 15, iy, 40, 35, NX_WHITE);
            fb_rect(ix + 15, iy, 40, 35, NX_DKGRAY);
            // Corner fold
            fb_fillrect(ix + 45, iy, 10, 10, NX_LTGRAY);
            for (int d = 0; d < 10; d++) fb_putpixel(ix + 45 + d, iy + d, NX_DKGRAY);
            // Text lines
            for (int l = 0; l < 4; l++)
                fb_hline(ix + 20, iy + 14 + l * 4, 25, NX_LTGRAY);
        }

        // Label
        int tw = strlen(items[i]) * font_char_width(FONT_SMALL);
        uint32_t lbg = is_sel ? RGB(200, 210, 240) : NX_WHITE;
        font_draw_string(ix + (icon_w - tw) / 2, iy + 40, items[i], NX_BLACK, lbg, FONT_SMALL);
    }

    // Status bar
    fb_fillrect(cx, cy + ch - 16, cw, 16, NX_LTGRAY);
    char status[40];
    ksprintf(status, "%d items", count);
    font_draw_string(cx + 8, cy + ch - 13, status, NX_DKGRAY, NX_LTGRAY, FONT_SMALL);
}

static void workspace_mouse(Window* win, int mx, int my, bool left, bool right) {
    (void)right;
    if (!left) return;
    WorkspaceState* ws = (WorkspaceState*)win->userdata;
    if (!ws) return;

    int cx, cy, cw, ch;
    wm_get_content_area(win, &cx, &cy, &cw, &ch);

    // Back button
    if (ws->depth > 0 && my >= cy && my < cy + 24 && mx >= cx + 4 && mx < cx + 44) {
        ws->depth = 0;
        ws->selected = -1;
        return;
    }

    // Icon click
    int icon_w = 70, icon_h = 60;
    int iy_start = cy + 30;

    const char** items = (ws->depth > 0) ? ws_sub_items[ws->folder] : ws_root_items;
    int count = (ws->depth > 0) ? ws_sub_counts[ws->folder] : ws_root_count;

    for (int i = 0; i < count; i++) {
        if (items[i][0] == 0) continue;
        int ix = cx + 10 + (i % 5) * (icon_w + 10);
        int iy = iy_start + (i / 5) * (icon_h + 10);
        if (mx >= ix && mx < ix + icon_w && my >= iy && my < iy + icon_h) {
            if (ws->selected == i && ws->depth == 0) {
                // Double-click effect: enter folder
                ws->depth = 1;
                ws->folder = i;
                ws->selected = -1;
            } else {
                ws->selected = i;
            }
            return;
        }
    }
    ws->selected = -1;
}

static void workspace_key(Window* win, uint8_t key) {
    WorkspaceState* ws = (WorkspaceState*)win->userdata;
    if (!ws) return;
    if (key == KEY_BACKSPACE && ws->depth > 0) {
        ws->depth = 0;
        ws->selected = -1;
    }
}

static void workspace_close(Window* win) { if (win->userdata) kfree(win->userdata); }

extern "C" void app_launch_workspace() {
    Window* w = wm_create_window("Workspace Manager", 60, 40, 420, 300,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE | WIN_RESIZABLE);
    if (!w) return;
    WorkspaceState* ws = (WorkspaceState*)kmalloc(sizeof(WorkspaceState));
    ws->selected = -1;
    ws->depth = 0;
    ws->folder = 0;
    w->userdata = ws;
    w->on_draw = workspace_draw;
    w->on_mouse = workspace_mouse;
    w->on_key = workspace_key;
    w->on_close = workspace_close;
}

// ============================================================
// MAIL - Mail client with compose, reply, delete
// ============================================================
#define MAIL_MAX_MSGS 8
#define MAIL_BODY_LEN 256

struct MailMsg {
    char sender[24];
    char subject[32];
    char body[MAIL_BODY_LEN];
    bool deleted;
};

struct MailState {
    MailMsg msgs[MAIL_MAX_MSGS];
    int msg_count;
    int selected;
    bool composing;     // true = compose mode
    bool replying;      // true = replying to selected
    char compose_to[32];
    char compose_subj[32];
    char compose_body[MAIL_BODY_LEN];
    int compose_len;
    int compose_cursor;
    int compose_field;  // 0=to, 1=subj, 2=body
    int to_len, subj_len;
};

static void mail_init_msgs(MailState* ms) {
    ms->msg_count = 3;
    strcpy(ms->msgs[0].sender, "System");
    strcpy(ms->msgs[0].subject, "Welcome!");
    strcpy(ms->msgs[0].body, "Welcome to Gateway OS2!\nYour system is ready.\n\nEnjoy exploring all the\nfeatures and applications.");
    ms->msgs[0].deleted = false;

    strcpy(ms->msgs[1].sender, "Admin");
    strcpy(ms->msgs[1].subject, "Setup Complete");
    strcpy(ms->msgs[1].body, "System setup is complete.\nAll 47 applications have\nbeen installed and are\nready to use.");
    ms->msgs[1].deleted = false;

    strcpy(ms->msgs[2].sender, "Gateway OS2");
    strcpy(ms->msgs[2].subject, "Getting Started");
    strcpy(ms->msgs[2].body, "Getting Started Guide:\n- Use the menubar at top\n- Right-click for context\n- Click dock items\n- Alt+Tab switches windows");
    ms->msgs[2].deleted = false;
}

static void mail_draw(Window* win, int cx, int cy, int cw, int ch) {
    MailState* ms = (MailState*)win->userdata;
    if (!ms) return;

    fb_fillrect(cx, cy, cw, ch, NX_WHITE);

    // Toolbar
    fb_fillrect(cx, cy, cw, 24, NX_LTGRAY);

    if (ms->composing) {
        nx_draw_button(cx + 4, cy + 2, 60, 18, "Send", false, true);
        nx_draw_button(cx + 68, cy + 2, 60, 18, "Cancel", false, false);
        nx_draw_separator(cx, cy + 24, cw);

        // Compose form
        int fy = cy + 30;
        bool f0 = (ms->compose_field == 0);
        bool f1 = (ms->compose_field == 1);

        font_draw_string(cx + 8, fy, "To:", NX_BLACK, NX_WHITE, FONT_SMALL);
        nx_draw_sunken(cx + 40, fy - 2, cw - 50, 16, NX_WHITE);
        font_draw_string(cx + 44, fy, ms->compose_to, NX_BLACK, NX_WHITE, FONT_SMALL);
        if (f0 && (timer_get_ticks() / 50) % 2 == 0) {
            int cx2 = cx + 44 + ms->to_len * font_char_width(FONT_SMALL);
            fb_fillrect(cx2, fy, 1, 10, NX_BLACK);
        }
        if (f0) fb_rect(cx + 39, fy - 3, cw - 48, 18, NX_BLACK);

        fy += 20;
        font_draw_string(cx + 8, fy, "Subj:", NX_BLACK, NX_WHITE, FONT_SMALL);
        nx_draw_sunken(cx + 40, fy - 2, cw - 50, 16, NX_WHITE);
        font_draw_string(cx + 44, fy, ms->compose_subj, NX_BLACK, NX_WHITE, FONT_SMALL);
        if (f1 && (timer_get_ticks() / 50) % 2 == 0) {
            int cx2 = cx + 44 + ms->subj_len * font_char_width(FONT_SMALL);
            fb_fillrect(cx2, fy, 1, 10, NX_BLACK);
        }
        if (f1) fb_rect(cx + 39, fy - 3, cw - 48, 18, NX_BLACK);

        fy += 22;
        nx_draw_separator(cx + 4, fy, cw - 8);
        fy += 4;

        // Body area
        fb_fillrect(cx + 4, fy, cw - 8, ch - (fy - cy) - 4, NX_WHITE);
        fb_rect(cx + 4, fy, cw - 8, ch - (fy - cy) - 4, ms->compose_field == 2 ? NX_BLACK : NX_DKGRAY);

        int bx = cx + 8, by = fy + 4;
        int char_w = font_char_width(FONT_SMALL);
        int max_x = cx + cw - 12;
        for (int i = 0; i < ms->compose_len; i++) {
            if (ms->compose_body[i] == '\n' || bx + char_w > max_x) {
                bx = cx + 8;
                by += 11;
                if (ms->compose_body[i] == '\n') continue;
            }
            font_draw_char(bx, by, ms->compose_body[i], NX_BLACK, NX_WHITE, FONT_SMALL);
            bx += char_w;
        }
        if (ms->compose_field == 2 && (timer_get_ticks() / 50) % 2 == 0)
            fb_fillrect(bx, by, 1, 9, NX_BLACK);

        font_draw_string(cx + 8, cy + ch - 14, "Tab: next field  Enter: newline", NX_DKGRAY, NX_WHITE, FONT_SMALL);
    } else {
        // Inbox mode
        nx_draw_button(cx + 4, cy + 2, 60, 18, "New", false, false);
        nx_draw_button(cx + 68, cy + 2, 60, 18, "Reply", false, false);
        nx_draw_button(cx + 132, cy + 2, 60, 18, "Delete", false, false);
        nx_draw_separator(cx, cy + 24, cw);

        // Mail list (left panel)
        int panel_w = cw / 2;
        fb_fillrect(cx, cy + 24, panel_w, ch - 24, NX_WHITE);
        fb_vline(cx + panel_w, cy + 24, ch - 24, NX_DKGRAY);

        int vis = 0;
        for (int i = 0; i < ms->msg_count; i++) {
            if (ms->msgs[i].deleted) continue;
            int y = cy + 28 + vis * 30;
            if (y + 30 > cy + ch) break;
            uint32_t bg = (i == ms->selected) ? RGB(200, 210, 240) : NX_WHITE;
            fb_fillrect(cx + 1, y - 2, panel_w - 2, 28, bg);
            // Unread dot
            if (i != ms->selected) {
                for (int dy = -2; dy <= 2; dy++)
                    for (int dx = -2; dx <= 2; dx++)
                        if (dx*dx+dy*dy <= 4) fb_putpixel(cx + 6 + dx, y + 5 + dy, RGB(0, 100, 220));
            }
            font_draw_string(cx + 14, y, ms->msgs[i].sender, NX_BLACK, bg, FONT_SMALL);
            font_draw_string(cx + 14, y + 12, ms->msgs[i].subject, NX_DKGRAY, bg, FONT_SMALL);
            if (vis < (ch - 28) / 30 - 1) nx_draw_separator(cx + 4, y + 26, panel_w - 8);
            vis++;
        }

        // Preview pane
        if (ms->selected >= 0 && ms->selected < ms->msg_count && !ms->msgs[ms->selected].deleted) {
            int px = cx + panel_w + 8;
            fb_fillrect(cx + panel_w + 1, cy + 24, cw - panel_w - 1, ch - 24, NX_WHITE);
            char buf[48];
            ksprintf(buf, "From: %s", ms->msgs[ms->selected].sender);
            font_draw_string(px, cy + 30, buf, NX_BLACK, NX_WHITE, FONT_SMALL);
            ksprintf(buf, "Subject: %s", ms->msgs[ms->selected].subject);
            font_draw_string(px, cy + 44, buf, NX_BLACK, NX_WHITE, FONT_SMALL);
            nx_draw_separator(px - 4, cy + 58, cw - panel_w - 12);

            // Render body with newlines
            int by = cy + 66;
            int bx = px;
            for (int c = 0; ms->msgs[ms->selected].body[c] && by < cy + ch - 4; c++) {
                if (ms->msgs[ms->selected].body[c] == '\n') {
                    bx = px; by += 14; continue;
                }
                font_draw_char(bx, by, ms->msgs[ms->selected].body[c], NX_BLACK, NX_WHITE, FONT_SMALL);
                bx += font_char_width(FONT_SMALL);
            }
        }
    }
}

static void mail_mouse(Window* win, int mx, int my, bool left, bool right) {
    (void)right;
    if (!left) return;
    MailState* ms = (MailState*)win->userdata;
    if (!ms) return;

    int cx, cy, cw, ch;
    wm_get_content_area(win, &cx, &cy, &cw, &ch);

    // Toolbar buttons
    if (my >= cy && my < cy + 24) {
        if (ms->composing) {
            if (mx >= cx + 4 && mx < cx + 64) {
                // Send — add message to inbox
                if (ms->compose_body[0] && ms->msg_count < MAIL_MAX_MSGS) {
                    MailMsg* m = &ms->msgs[ms->msg_count];
                    strcpy(m->sender, "You");
                    if (ms->compose_subj[0])
                        strcpy(m->subject, ms->compose_subj);
                    else
                        strcpy(m->subject, "(no subject)");
                    strcpy(m->body, ms->compose_body);
                    m->deleted = false;
                    ms->selected = ms->msg_count;
                    ms->msg_count++;
                }
                ms->composing = false;
            } else if (mx >= cx + 68 && mx < cx + 128) {
                // Cancel
                ms->composing = false;
            }
        } else {
            if (mx >= cx + 4 && mx < cx + 64) {
                // New
                ms->composing = true;
                ms->replying = false;
                ms->compose_to[0] = 0; ms->to_len = 0;
                ms->compose_subj[0] = 0; ms->subj_len = 0;
                ms->compose_body[0] = 0; ms->compose_len = 0;
                ms->compose_cursor = 0;
                ms->compose_field = 0;
            } else if (mx >= cx + 68 && mx < cx + 128) {
                // Reply
                if (ms->selected >= 0 && ms->selected < ms->msg_count && !ms->msgs[ms->selected].deleted) {
                    ms->composing = true;
                    ms->replying = true;
                    strcpy(ms->compose_to, ms->msgs[ms->selected].sender);
                    ms->to_len = strlen(ms->compose_to);
                    ksprintf(ms->compose_subj, "Re: %s", ms->msgs[ms->selected].subject);
                    ms->subj_len = strlen(ms->compose_subj);
                    ms->compose_body[0] = 0; ms->compose_len = 0;
                    ms->compose_cursor = 0;
                    ms->compose_field = 2; // Start in body
                }
            } else if (mx >= cx + 132 && mx < cx + 192) {
                // Delete
                if (ms->selected >= 0 && ms->selected < ms->msg_count) {
                    ms->msgs[ms->selected].deleted = true;
                    // Select next non-deleted
                    for (int i = 0; i < ms->msg_count; i++) {
                        if (!ms->msgs[i].deleted) { ms->selected = i; break; }
                    }
                }
            }
        }
        return;
    }

    if (ms->composing) {
        // Click on fields
        int fy = cy + 30;
        if (my >= fy - 2 && my < fy + 16) { ms->compose_field = 0; return; }
        fy += 20;
        if (my >= fy - 2 && my < fy + 16) { ms->compose_field = 1; return; }
        if (my > fy + 20) { ms->compose_field = 2; return; }
        return;
    }

    // Click in mail list
    int panel_w = cw / 2;
    if (mx < cx + panel_w && my > cy + 24) {
        int vis = 0;
        for (int i = 0; i < ms->msg_count; i++) {
            if (ms->msgs[i].deleted) continue;
            int y = cy + 28 + vis * 30;
            if (my >= y - 2 && my < y + 28) { ms->selected = i; return; }
            vis++;
        }
    }
}

static void mail_key(Window* win, uint8_t key) {
    MailState* ms = (MailState*)win->userdata;
    if (!ms) return;

    if (ms->composing) {
        if (key == KEY_TAB) {
            ms->compose_field = (ms->compose_field + 1) % 3;
            return;
        }

        // Get pointer to current field
        char* buf;
        int* len;
        int maxlen;
        if (ms->compose_field == 0) {
            buf = ms->compose_to; len = &ms->to_len; maxlen = 30;
        } else if (ms->compose_field == 1) {
            buf = ms->compose_subj; len = &ms->subj_len; maxlen = 30;
        } else {
            buf = ms->compose_body; len = &ms->compose_len; maxlen = MAIL_BODY_LEN - 2;
        }

        if (key == KEY_BACKSPACE && *len > 0) {
            (*len)--;
            buf[*len] = 0;
        } else if (key == KEY_ENTER && ms->compose_field == 2 && *len < maxlen) {
            buf[*len] = '\n'; (*len)++; buf[*len] = 0;
        } else if (key >= 0x20 && key < 0x7F && *len < maxlen) {
            buf[*len] = (char)key; (*len)++; buf[*len] = 0;
        }
        return;
    }

    // Inbox navigation
    if (key == KEY_UP) {
        for (int i = ms->selected - 1; i >= 0; i--)
            if (!ms->msgs[i].deleted) { ms->selected = i; break; }
    }
    if (key == KEY_DOWN) {
        for (int i = ms->selected + 1; i < ms->msg_count; i++)
            if (!ms->msgs[i].deleted) { ms->selected = i; break; }
    }
}

static void mail_close(Window* win) { if (win->userdata) kfree(win->userdata); }

extern "C" void app_launch_mail() {
    Window* w = wm_create_window("Mail", 100, 60, 500, 300,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE | WIN_RESIZABLE);
    if (!w) return;
    MailState* ms = (MailState*)kmalloc(sizeof(MailState));
    memset(ms, 0, sizeof(MailState));
    mail_init_msgs(ms);
    ms->selected = 0;
    w->userdata = ms;
    w->on_draw = mail_draw;
    w->on_mouse = mail_mouse;
    w->on_key = mail_key;
    w->on_close = mail_close;
}

// ============================================================
// GRAB - Screenshot tool (captures live screen thumbnail)
// ============================================================
struct GrabState {
    bool captured;
    uint32_t thumbnail[160 * 120]; // Downscaled screenshot
};

static void grab_capture(GrabState* gs) {
    // Sample the backbuffer at 1/6 resolution -> 170x128, store 160x120
    uint32_t* bb = fb_get_backbuffer();
    if (!bb) return;
    for (int ty = 0; ty < 120; ty++) {
        int sy = ty * SCREEN_HEIGHT / 120;
        for (int tx = 0; tx < 160; tx++) {
            int sx = tx * SCREEN_WIDTH / 160;
            gs->thumbnail[ty * 160 + tx] = bb[sy * SCREEN_WIDTH + sx];
        }
    }
    gs->captured = true;
}

static void grab_draw(Window* win, int cx, int cy, int cw, int ch) {
    GrabState* gs = (GrabState*)win->userdata;
    if (!gs) return;

    fb_fillrect(cx, cy, cw, ch, NX_LTGRAY);

    // Header
    font_draw_string(cx + 8, cy + 4, "Grab - Screenshot", NX_BLACK, NX_LTGRAY, FONT_MEDIUM);
    nx_draw_separator(cx + 4, cy + 20, cw - 8);

    if (gs->captured) {
        // Draw thumbnail with border
        int thumb_x = cx + (cw - 164) / 2;
        int thumb_y = cy + 28;
        nx_draw_sunken(thumb_x - 2, thumb_y - 2, 164, 124, NX_BLACK);
        for (int ty = 0; ty < 120; ty++) {
            for (int tx = 0; tx < 160; tx++) {
                fb_putpixel(thumb_x + tx, thumb_y + ty, gs->thumbnail[ty * 160 + tx]);
            }
        }

        // Info
        font_draw_string(cx + 8, thumb_y + 128, "1024x768 captured", NX_DKGRAY, NX_LTGRAY, FONT_SMALL);
    } else {
        font_draw_string(cx + 8, cy + 60, "No screenshot captured yet.", NX_DKGRAY, NX_LTGRAY, FONT_SMALL);
    }

    // Capture button
    int by = cy + ch - 26;
    nx_draw_button(cx + (cw - 100) / 2, by, 100, 20, "Capture", false, true);
    font_draw_string(cx + 8, by + 4, "Enter", NX_DKGRAY, NX_LTGRAY, FONT_SMALL);
}

static void grab_key(Window* win, uint8_t key) {
    GrabState* gs = (GrabState*)win->userdata;
    if (!gs) return;
    if (key == KEY_ENTER) grab_capture(gs);
}

static void grab_mouse(Window* win, int mx, int my, bool left, bool right) {
    (void)right;
    if (!left) return;
    GrabState* gs = (GrabState*)win->userdata;
    if (!gs) return;
    int cx, cy, cw, ch;
    wm_get_content_area(win, &cx, &cy, &cw, &ch);
    int by = cy + ch - 26;
    if (my >= by && my < by + 20 && mx >= cx + (cw - 100) / 2 && mx < cx + (cw + 100) / 2) {
        grab_capture(gs);
    }
}

static void grab_close(Window* win) { if (win->userdata) kfree(win->userdata); }

extern "C" void app_launch_grab() {
    Window* w = wm_create_window("Grab", 180, 120, 260, 210,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE);
    if (!w) return;
    GrabState* gs = (GrabState*)kmalloc(sizeof(GrabState));
    memset(gs, 0, sizeof(GrabState));
    w->userdata = gs;
    w->on_draw = grab_draw;
    w->on_key = grab_key;
    w->on_mouse = grab_mouse;
    w->on_close = grab_close;
}
