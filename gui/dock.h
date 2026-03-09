#pragma once

#include "../lib/types.h"

#define DOCK_TILE_SIZE 64
#define DOCK_ICON_SIZE 48
#define DOCK_MAX_ITEMS 16

struct DockItem {
    char name[24];
    void (*launcher)();
    bool running;
    bool is_separator;
    // Simple icon: 48x48 stored as indices into a small color table
    uint8_t icon[DOCK_ICON_SIZE * DOCK_ICON_SIZE];
};

void dock_init();
void dock_add_item(const char* name, void (*launcher)(), const uint8_t* icon);
void dock_add_separator();
void dock_set_running(const char* name, bool running);
void dock_draw();
int dock_hit_test(int mx, int my); // Returns item index or -1
void dock_launch(int index);       // Launch dock item by index
int dock_get_x(); // Left edge of dock
