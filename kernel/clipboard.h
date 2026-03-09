#pragma once

#include "../lib/types.h"

#define CLIPBOARD_MAX 4096

void clipboard_init();
void clipboard_copy(const char* text, int len);
const char* clipboard_paste(int* out_len);
void clipboard_clear();
