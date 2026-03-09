#pragma once

#include "../lib/types.h"

#define MENU_ITEM_HEIGHT 22
#define MENU_WIDTH 160
#define MENU_HEADER_HEIGHT 23
#define MAX_MENU_ITEMS 20
#define MAX_SUBMENUS 16
#define MENUBAR_HEIGHT 22

struct MenuItem {
    char label[32];
    char shortcut[8];       // e.g. "Cmd+Q"
    void (*action)();
    int submenu_id;         // -1 if no submenu
    bool is_separator;
    bool enabled;
};

struct Menu {
    char title[32];
    MenuItem items[MAX_MENU_ITEMS];
    int item_count;
    int x, y;               // Position on screen
    int highlight;           // Currently highlighted item (-1 = none)
    bool visible;
    bool torn_off;           // Has been torn off as floating window
    int parent_id;           // -1 if root menu
};

// Menu system
void menu_init();
int menu_create(const char* title, int parent_id);
void menu_add_item(int menu_id, const char* label, const char* shortcut, void (*action)());
void menu_add_submenu(int menu_id, const char* label, int submenu_id);
void menu_add_separator(int menu_id);

// Show/hide
void menu_show(int menu_id, int x, int y);
void menu_hide(int menu_id);
void menu_hide_all();

// Drawing
void menu_draw_all();

// Input
bool menu_handle_mouse(int mx, int my, bool clicked);
bool menu_is_active(); // Any menu visible?

// The main application menu
void menu_setup_main_menu();
int menu_get_main_id();

// Top menu bar
void menubar_draw();
bool menubar_handle_mouse(int mx, int my, bool clicked);

// Right-click context menu
void menu_show_context(int mx, int my);
int menu_get_context_id();
