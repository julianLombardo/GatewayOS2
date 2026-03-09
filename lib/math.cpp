#include "math.h"

// Sine table * 256 for 0-90 degrees
static const int16_t sin_table[91] = {
    0,4,9,13,18,22,27,31,36,40,44,49,53,57,62,66,70,74,79,83,
    87,91,95,99,103,107,111,115,119,122,126,130,133,137,140,143,
    147,150,153,156,159,162,165,167,170,173,175,178,180,182,184,
    187,189,191,193,194,196,198,199,201,202,204,205,206,207,208,
    209,210,211,212,213,214,214,215,215,216,216,217,217,217,217,
    217,218,217,217,217,217,217,216,216,256
};

int sin256(int angle_deg) {
    angle_deg = ((angle_deg % 360) + 360) % 360;
    int sign = 1;
    if (angle_deg >= 180) { sign = -1; angle_deg -= 180; }
    if (angle_deg > 90) angle_deg = 180 - angle_deg;
    return sign * sin_table[angle_deg];
}

int cos256(int angle_deg) {
    return sin256(angle_deg + 90);
}

uint32_t isqrt(uint32_t n) {
    if (n == 0) return 0;
    uint32_t x = n;
    uint32_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

static uint32_t rand_state = 12345;

void srand(uint32_t seed) { rand_state = seed; }

uint32_t rand() {
    rand_state = rand_state * 1103515245 + 12345;
    return (rand_state >> 16) & 0x7FFF;
}
