#pragma once

#include "../lib/types.h"

void speaker_tone(uint32_t freq);
void speaker_off();
void speaker_beep(uint32_t freq, uint32_t ms);
void speaker_boot_melody();
