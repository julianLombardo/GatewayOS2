#include "menu.h"
#include "theme.h"
#include "font.h"
#include "../drivers/framebuffer.h"
#include "../lib/string.h"

static Menu menus[MAX_SUBMENUS + 2]; // extra room
static int menu_count = 0;
static int main_menu_id = -1;
static int context_menu_id = -1;

// Menubar: list of top-level menu labels and their submenu IDs
#define MAX_MENUBAR_ITEMS 10
struct MenuBarItem {
    char label[20];
    int submenu_id;
    int x, w; // pixel position and width on the bar
};
static MenuBarItem menubar_items[MAX_MENUBAR_ITEMS];
static int menubar_item_count = 0;
static int menubar_active = -1; // Which menubar item is currently open

void menu_init() {
    memset(menus, 0, sizeof(menus));
    menu_count = 0;
    main_menu_id = -1;
}

int menu_create(const char* title, int parent_id) {
    if (menu_count >= MAX_SUBMENUS + 1) return -1;
    int id = menu_count++;
    Menu* m = &menus[id];
    strncpy(m->title, title, 31);
    m->item_count = 0;
    m->x = 0; m->y = 0;
    m->highlight = -1;
    m->visible = false;
    m->torn_off = false;
    m->parent_id = parent_id;
    return id;
}

void menu_add_item(int menu_id, const char* label, const char* shortcut, void (*action)()) {
    if (menu_id < 0 || menu_id >= menu_count) return;
    Menu* m = &menus[menu_id];
    if (m->item_count >= MAX_MENU_ITEMS) return;
    MenuItem* item = &m->items[m->item_count++];
    strncpy(item->label, label, 31);
    if (shortcut) strncpy(item->shortcut, shortcut, 7);
    else item->shortcut[0] = 0;
    item->action = action;
    item->submenu_id = -1;
    item->is_separator = false;
    item->enabled = true;
}

void menu_add_submenu(int menu_id, const char* label, int submenu_id) {
    if (menu_id < 0 || menu_id >= menu_count) return;
    Menu* m = &menus[menu_id];
    if (m->item_count >= MAX_MENU_ITEMS) return;
    MenuItem* item = &m->items[m->item_count++];
    strncpy(item->label, label, 31);
    item->shortcut[0] = 0;
    item->action = NULL;
    item->submenu_id = submenu_id;
    item->is_separator = false;
    item->enabled = true;
}

void menu_add_separator(int menu_id) {
    if (menu_id < 0 || menu_id >= menu_count) return;
    Menu* m = &menus[menu_id];
    if (m->item_count >= MAX_MENU_ITEMS) return;
    MenuItem* item = &m->items[m->item_count++];
    item->label[0] = 0;
    item->shortcut[0] = 0;
    item->action = NULL;
    item->submenu_id = -1;
    item->is_separator = true;
    item->enabled = true;
}

void menu_show(int menu_id, int x, int y) {
    if (menu_id < 0 || menu_id >= menu_count) return;
    menus[menu_id].x = x;
    menus[menu_id].y = y;
    menus[menu_id].visible = true;
    menus[menu_id].highlight = -1;
}

void menu_hide(int menu_id) {
    if (menu_id < 0 || menu_id >= menu_count) return;
    if (menus[menu_id].torn_off) return; // Don't hide torn-off menus
    menus[menu_id].visible = false;
    menus[menu_id].highlight = -1;
    // Hide any child submenus
    for (int i = 0; i < menus[menu_id].item_count; i++) {
        if (menus[menu_id].items[i].submenu_id >= 0)
            menu_hide(menus[menu_id].items[i].submenu_id);
    }
}

void menu_hide_all() {
    for (int i = 0; i < menu_count; i++) {
        if (!menus[i].torn_off) {
            menus[i].visible = false;
            menus[i].highlight = -1;
        }
    }
    menubar_active = -1;
}

static void draw_menu(Menu* m) {
    if (!m->visible) return;

    int w = MENU_WIDTH;
    int item_h = MENU_ITEM_HEIGHT;
    int total_h = MENU_HEADER_HEIGHT;
    for (int i = 0; i < m->item_count; i++)
        total_h += m->items[i].is_separator ? 6 : item_h;

    // Shadow
    nx_draw_shadow(m->x, m->y, w, total_h, 3);

    // Menu background
    fb_fillrect(m->x, m->y, w, total_h, NX_LTGRAY);
    fb_rect(m->x, m->y, w, total_h, NX_BLACK);

    // Header (black background, white text)
    fb_fillrect(m->x + 1, m->y + 1, w - 2, MENU_HEADER_HEIGHT - 1, NX_BLACK);
    int tw = font_string_width(m->title, FONT_MEDIUM);
    int tx = m->x + (w - tw) / 2;
    font_draw_string_nobg(tx, m->y + 5, m->title, NX_WHITE, FONT_MEDIUM);

    // Close button on header (if torn off)
    if (m->torn_off) {
        int bx = m->x + w - 18;
        int by = m->y + 4;
        fb_fillrect(bx, by, 13, 13, NX_DKGRAY);
        fb_rect(bx, by, 13, 13, NX_WHITE);
        // X mark
        for (int i = 0; i < 5; i++) {
            fb_putpixel(bx + 4 + i, by + 4 + i, NX_WHITE);
            fb_putpixel(bx + 8 - i, by + 4 + i, NX_WHITE);
        }
    }

    // Menu items
    int y = m->y + MENU_HEADER_HEIGHT;
    for (int i = 0; i < m->item_count; i++) {
        MenuItem* item = &m->items[i];

        if (item->is_separator) {
            nx_draw_separator(m->x + 4, y + 2, w - 8);
            y += 6;
            continue;
        }

        // Highlight
        if (i == m->highlight) {
            fb_fillrect(m->x + 1, y, w - 2, item_h, NX_BLACK);
            font_draw_string_nobg(m->x + 12, y + 5, item->label, NX_WHITE, FONT_MEDIUM);
            if (item->shortcut[0])
                font_draw_string_nobg(m->x + w - font_string_width(item->shortcut, FONT_SMALL) - 8,
                                      y + 7, item->shortcut, NX_WHITE, FONT_SMALL);
        } else {
            uint32_t fg = item->enabled ? NX_BLACK : NX_DKGRAY;
            font_draw_string_nobg(m->x + 12, y + 5, item->label, fg, FONT_MEDIUM);
            if (item->shortcut[0])
                font_draw_string_nobg(m->x + w - font_string_width(item->shortcut, FONT_SMALL) - 8,
                                      y + 7, item->shortcut, fg, FONT_SMALL);
        }

        // Submenu arrow
        if (item->submenu_id >= 0) {
            int ax = m->x + w - 14;
            int ay = y + item_h / 2;
            for (int r = 0; r < 4; r++) {
                uint32_t ac = (i == m->highlight) ? NX_WHITE : NX_BLACK;
                fb_vline(ax + r, ay - r, r * 2 + 1, ac);
            }
        }

        y += item_h;
    }
}

void menu_draw_all() {
    for (int i = 0; i < menu_count; i++)
        draw_menu(&menus[i]);
}

bool menu_handle_mouse(int mx, int my, bool clicked) {
    for (int i = menu_count - 1; i >= 0; i--) {
        Menu* m = &menus[i];
        if (!m->visible) continue;

        int w = MENU_WIDTH;
        int total_h = MENU_HEADER_HEIGHT;
        for (int j = 0; j < m->item_count; j++)
            total_h += m->items[j].is_separator ? 6 : MENU_ITEM_HEIGHT;

        if (mx < m->x || mx >= m->x + w || my < m->y || my >= m->y + total_h)
            continue;

        // In header area?
        if (my < m->y + MENU_HEADER_HEIGHT) {
            // Close button for torn-off menus
            if (m->torn_off && clicked && mx >= m->x + w - 18) {
                m->visible = false;
                m->torn_off = false;
                return true;
            }
            return true;
        }

        // Find which item
        int y = m->y + MENU_HEADER_HEIGHT;
        for (int j = 0; j < m->item_count; j++) {
            int ih = m->items[j].is_separator ? 6 : MENU_ITEM_HEIGHT;
            if (my >= y && my < y + ih && !m->items[j].is_separator) {
                m->highlight = j;

                // Show submenu
                if (m->items[j].submenu_id >= 0) {
                    menu_show(m->items[j].submenu_id, m->x + w - 4, y);
                }

                // Execute on click
                if (clicked && m->items[j].action && m->items[j].enabled) {
                    m->items[j].action();
                    menu_hide_all();
                    return true;
                }
                return true;
            }
            y += ih;
        }
        return true;
    }

    // Clicked outside all menus
    if (clicked) {
        menu_hide_all();
    }
    return false;
}

bool menu_is_active() {
    for (int i = 0; i < menu_count; i++)
        if (menus[i].visible) return true;
    return false;
}

// Forward declarations for menu actions (C linkage - defined in kernel.cpp)
extern "C" {
    void app_launch_workspace();
    void app_launch_terminal();
    void app_launch_edit();
    void app_launch_mail();
    void app_launch_preferences();
    void app_launch_help();
    void app_launch_calculator();
    void app_launch_clock();
    void app_launch_chess();
    void app_launch_snake();
    void app_launch_pong();
    void app_launch_mines();
    void app_launch_tetris();
    void app_launch_puzzle();
    void app_launch_billiards();
    void app_launch_paint();
    void app_launch_draw();
    void app_launch_decrypt();
    void app_launch_radar();
    void app_launch_neural();
    void app_launch_uplink();
    void app_launch_probe();
    void app_launch_starmap();
    void app_launch_comm();
    void app_launch_matrix();
    void app_launch_sysmon();
    void app_launch_netinfo();
    void app_launch_hexview();
    void app_launch_grab();
    void app_launch_about();
    // Intelligence Tools
    void app_launch_cipher();
    void app_launch_fortress();
    void app_launch_sentinel();
    void app_launch_netscan();
    void app_launch_hashlab();
    // Extras
    void app_launch_calendar();
    void app_launch_notes();
    void app_launch_contacts();
    void app_launch_colorpick();
    void app_launch_fontview();
    void app_launch_taskmgr();
    void app_launch_diskuse();
    void app_launch_logview();
    void app_launch_screensaver();
    void app_launch_weather();
    void app_launch_music();
    void app_launch_fileview();
    void app_launch_gmail();
    void app_launch_javaide();
    void app_launch_perun();
}
extern void desktop_request_shutdown();
extern void desktop_request_hide();

static void menubar_add(const char* label, int submenu_id) {
    if (menubar_item_count >= MAX_MENUBAR_ITEMS) return;
    MenuBarItem* item = &menubar_items[menubar_item_count];
    strncpy(item->label, label, 19);
    item->label[19] = 0;
    item->submenu_id = submenu_id;
    // Calculate x position
    int padding = 12;
    if (menubar_item_count == 0) {
        item->x = 4;
    } else {
        MenuBarItem* prev = &menubar_items[menubar_item_count - 1];
        item->x = prev->x + prev->w + 2;
    }
    item->w = font_string_width(label, FONT_SMALL) + padding * 2;
    menubar_item_count++;
}

void menu_setup_main_menu() {
    menubar_item_count = 0;
    menubar_active = -1;

    // Main menu (for vertical NeXT menu - keep for compatibility but hide it)
    main_menu_id = menu_create("Gateway OS2", -1);

    // === MENUBAR DROPDOWN MENUS ===

    // System submenu
    int sys_menu = menu_create("System", -1);
    menu_add_item(sys_menu, "About...", NULL, app_launch_about);
    menu_add_separator(sys_menu);
    menu_add_item(sys_menu, "Workspace", NULL, app_launch_workspace);
    menu_add_item(sys_menu, "Terminal", NULL, app_launch_terminal);
    menu_add_separator(sys_menu);
    menu_add_item(sys_menu, "Preferences", NULL, app_launch_preferences);
    menu_add_item(sys_menu, "Help", NULL, app_launch_help);
    menu_add_separator(sys_menu);
    menu_add_item(sys_menu, "Hide", "Alt+H", desktop_request_hide);
    menu_add_item(sys_menu, "Quit", "Alt+Q", desktop_request_shutdown);

    // Productivity submenu
    int prod_menu = menu_create("Productivity", -1);
    menu_add_item(prod_menu, "Java IDE", NULL, app_launch_javaide);
    menu_add_item(prod_menu, "Edit", NULL, app_launch_edit);
    menu_add_item(prod_menu, "Gateway Mail", NULL, app_launch_gmail);
    menu_add_item(prod_menu, "Mail (Local)", NULL, app_launch_mail);
    menu_add_item(prod_menu, "Notes", NULL, app_launch_notes);
    menu_add_item(prod_menu, "Contacts", NULL, app_launch_contacts);
    menu_add_separator(prod_menu);
    menu_add_item(prod_menu, "Calculator", NULL, app_launch_calculator);
    menu_add_item(prod_menu, "Clock", NULL, app_launch_clock);
    menu_add_item(prod_menu, "Calendar", NULL, app_launch_calendar);

    // Creative submenu
    int create_menu = menu_create("Creative", -1);
    menu_add_item(create_menu, "Draw", NULL, app_launch_draw);
    menu_add_item(create_menu, "Paint", NULL, app_launch_paint);
    menu_add_item(create_menu, "Color Picker", NULL, app_launch_colorpick);
    menu_add_item(create_menu, "Font Viewer", NULL, app_launch_fontview);
    menu_add_separator(create_menu);
    menu_add_item(create_menu, "Music Player", NULL, app_launch_music);

    // Games submenu
    int games_menu = menu_create("Games", -1);
    menu_add_item(games_menu, "Chess", NULL, app_launch_chess);
    menu_add_item(games_menu, "Billiards", NULL, app_launch_billiards);
    menu_add_item(games_menu, "Puzzle", NULL, app_launch_puzzle);
    menu_add_separator(games_menu);
    menu_add_item(games_menu, "Snake", NULL, app_launch_snake);
    menu_add_item(games_menu, "Pong", NULL, app_launch_pong);
    menu_add_item(games_menu, "Minesweeper", NULL, app_launch_mines);
    menu_add_item(games_menu, "Tetris", NULL, app_launch_tetris);

    // Sci-Fi submenu
    int scifi_menu = menu_create("Sci-Fi", -1);
    menu_add_item(scifi_menu, "GW-Decrypt", NULL, app_launch_decrypt);
    menu_add_item(scifi_menu, "GW-Radar", NULL, app_launch_radar);
    menu_add_item(scifi_menu, "GW-Neural", NULL, app_launch_neural);
    menu_add_item(scifi_menu, "GW-Uplink", NULL, app_launch_uplink);
    menu_add_item(scifi_menu, "GW-Probe", NULL, app_launch_probe);
    menu_add_item(scifi_menu, "GW-StarMap", NULL, app_launch_starmap);
    menu_add_item(scifi_menu, "GW-Comm", NULL, app_launch_comm);
    menu_add_item(scifi_menu, "GW-Matrix", NULL, app_launch_matrix);

    // Tools submenu
    int tools_menu = menu_create("Tools", -1);
    menu_add_item(tools_menu, "System Monitor", NULL, app_launch_sysmon);
    menu_add_item(tools_menu, "Task Manager", NULL, app_launch_taskmgr);
    menu_add_item(tools_menu, "Network Info", NULL, app_launch_netinfo);
    menu_add_item(tools_menu, "Hex Viewer", NULL, app_launch_hexview);
    menu_add_item(tools_menu, "Disk Usage", NULL, app_launch_diskuse);
    menu_add_item(tools_menu, "File Viewer", NULL, app_launch_fileview);
    menu_add_separator(tools_menu);
    menu_add_item(tools_menu, "Log Viewer", NULL, app_launch_logview);
    menu_add_item(tools_menu, "Grab", NULL, app_launch_grab);
    menu_add_item(tools_menu, "Screensaver", NULL, app_launch_screensaver);
    menu_add_item(tools_menu, "Weather", NULL, app_launch_weather);
    menu_add_item(tools_menu, "PE32 Loader", NULL, app_launch_perun);

    // Intelligence submenu
    int intel_menu = menu_create("Intel", -1);
    menu_add_item(intel_menu, "GW-Cipher", NULL, app_launch_cipher);
    menu_add_item(intel_menu, "GW-Fortress", NULL, app_launch_fortress);
    menu_add_item(intel_menu, "GW-Sentinel", NULL, app_launch_sentinel);
    menu_add_item(intel_menu, "GW-NetScan", NULL, app_launch_netscan);
    menu_add_item(intel_menu, "GW-Hashlab", NULL, app_launch_hashlab);

    // Add items to menubar
    menubar_add("System", sys_menu);
    menubar_add("Productivity", prod_menu);
    menubar_add("Creative", create_menu);
    menubar_add("Games", games_menu);
    menubar_add("Sci-Fi", scifi_menu);
    menubar_add("Tools", tools_menu);
    menubar_add("Intel", intel_menu);

    // === RIGHT-CLICK CONTEXT MENU ===
    context_menu_id = menu_create("Desktop", -1);
    menu_add_item(context_menu_id, "New Terminal", NULL, app_launch_terminal);
    menu_add_item(context_menu_id, "New Note", NULL, app_launch_notes);
    menu_add_separator(context_menu_id);
    menu_add_item(context_menu_id, "Workspace", NULL, app_launch_workspace);
    menu_add_item(context_menu_id, "File Viewer", NULL, app_launch_fileview);
    menu_add_item(context_menu_id, "Calculator", NULL, app_launch_calculator);
    menu_add_separator(context_menu_id);
    menu_add_item(context_menu_id, "System Monitor", NULL, app_launch_sysmon);
    menu_add_item(context_menu_id, "Task Manager", NULL, app_launch_taskmgr);
    menu_add_item(context_menu_id, "Preferences", NULL, app_launch_preferences);
    menu_add_separator(context_menu_id);
    menu_add_item(context_menu_id, "About...", NULL, app_launch_about);
    menu_add_item(context_menu_id, "Quit", NULL, desktop_request_shutdown);
}

int menu_get_main_id() {
    return main_menu_id;
}

int menu_get_context_id() {
    return context_menu_id;
}

// ============================================================
// MENUBAR - Horizontal bar at top of screen
// ============================================================

void menubar_draw() {
    // Draw the bar background
    nx_draw_raised_color(0, 0, SCREEN_WIDTH, MENUBAR_HEIGHT, NX_LTGRAY);

    // OS name on the left (bold/highlighted)
    font_draw_string(8, 5, "Gateway OS2", NX_BLACK, NX_LTGRAY, FONT_SMALL);

    // Separator after OS name
    int sep_x = 8 + font_string_width("Gateway OS2", FONT_SMALL) + 8;
    fb_vline(sep_x, 2, MENUBAR_HEIGHT - 4, NX_DKGRAY);
    fb_vline(sep_x + 1, 2, MENUBAR_HEIGHT - 4, NX_WHITE);

    // Draw menu items
    for (int i = 0; i < menubar_item_count; i++) {
        // Offset all items after the OS name + separator
        int offset = sep_x + 6;
        int ix = menubar_items[i].x + offset;
        int iw = menubar_items[i].w;

        if (i == menubar_active) {
            // Pressed/active state
            fb_fillrect(ix, 1, iw, MENUBAR_HEIGHT - 2, NX_BLACK);
            int tw = font_string_width(menubar_items[i].label, FONT_SMALL);
            font_draw_string(ix + (iw - tw) / 2, 5, menubar_items[i].label, NX_WHITE, NX_BLACK, FONT_SMALL);
        } else {
            int tw = font_string_width(menubar_items[i].label, FONT_SMALL);
            font_draw_string(ix + (iw - tw) / 2, 5, menubar_items[i].label, NX_BLACK, NX_LTGRAY, FONT_SMALL);
        }
    }
}

bool menubar_handle_mouse(int mx, int my, bool clicked) {
    // Only respond to clicks in the menubar area
    if (my >= MENUBAR_HEIGHT) {
        // If a menu is open and user clicked outside, close it
        if (menubar_active >= 0 && clicked) {
            // Let the dropdown menu system handle it
        }
        return false;
    }

    if (!clicked) {
        // Hover: if a menu is already open, switch to hovered item
        if (menubar_active >= 0) {
            int sep_x = 8 + font_string_width("Gateway OS2", FONT_SMALL) + 8;
            int offset = sep_x + 6;
            for (int i = 0; i < menubar_item_count; i++) {
                int ix = menubar_items[i].x + offset;
                int iw = menubar_items[i].w;
                if (mx >= ix && mx < ix + iw) {
                    if (i != menubar_active) {
                        // Close old, open new
                        menu_hide(menubar_items[menubar_active].submenu_id);
                        menubar_active = i;
                        menu_show(menubar_items[i].submenu_id, ix, MENUBAR_HEIGHT);
                    }
                    return true;
                }
            }
        }
        return false;
    }

    // Click on menubar
    int sep_x = 8 + font_string_width("Gateway OS2", FONT_SMALL) + 8;
    int offset = sep_x + 6;

    for (int i = 0; i < menubar_item_count; i++) {
        int ix = menubar_items[i].x + offset;
        int iw = menubar_items[i].w;
        if (mx >= ix && mx < ix + iw) {
            if (menubar_active == i) {
                // Toggle off
                menu_hide(menubar_items[i].submenu_id);
                menubar_active = -1;
            } else {
                // Close any open menu
                if (menubar_active >= 0)
                    menu_hide(menubar_items[menubar_active].submenu_id);
                // Open this one
                menubar_active = i;
                menu_show(menubar_items[i].submenu_id, ix, MENUBAR_HEIGHT);
            }
            return true;
        }
    }

    return true; // Clicked on bar but not on an item
}

void menu_show_context(int mx, int my) {
    if (context_menu_id >= 0) {
        // Make sure it doesn't go off screen
        int menu_h = MENU_HEADER_HEIGHT;
        Menu* m = &menus[context_menu_id];
        for (int i = 0; i < m->item_count; i++)
            menu_h += m->items[i].is_separator ? 6 : MENU_ITEM_HEIGHT;

        if (mx + MENU_WIDTH > SCREEN_WIDTH) mx = SCREEN_WIDTH - MENU_WIDTH;
        if (my + menu_h > SCREEN_HEIGHT) my = SCREEN_HEIGHT - menu_h;
        if (mx < 0) mx = 0;
        if (my < 0) my = 0;

        menu_show(context_menu_id, mx, my);
    }
}
