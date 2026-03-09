#pragma once

#include "../lib/types.h"

void desktop_init();
void desktop_draw();
void desktop_handle_mouse(int mx, int my, bool left, bool right);
void desktop_handle_mouse_move(int mx, int my, bool left_held);
void desktop_handle_key(uint8_t key);
void desktop_request_shutdown();
void desktop_request_hide();
bool desktop_shutdown_requested();
