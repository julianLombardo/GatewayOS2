#pragma once

#include "../lib/types.h"

void serial_init();
void serial_write(const char* str);
void serial_write_char(char c);
