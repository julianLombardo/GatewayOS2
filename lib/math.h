#pragma once

#include "types.h"

int sin256(int angle_deg);
int cos256(int angle_deg);
uint32_t isqrt(uint32_t n);

// LCG random number generator
void srand(uint32_t seed);
uint32_t rand();
