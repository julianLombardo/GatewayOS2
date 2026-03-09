#pragma once

#include "../lib/types.h"

void timer_init(uint32_t frequency);
uint32_t timer_get_ticks();
void timer_sleep(uint32_t ms);
