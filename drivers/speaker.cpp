#include "speaker.h"
#include "ports.h"
#include "../kernel/timer.h"

void speaker_tone(uint32_t freq) {
    if (freq == 0) { speaker_off(); return; }
    uint32_t div = 1193180 / freq;
    outb(0x43, 0xB6);
    outb(0x42, div & 0xFF);
    outb(0x42, (div >> 8) & 0xFF);
    uint8_t tmp = inb(0x61);
    outb(0x61, tmp | 3);
}

void speaker_off() {
    uint8_t tmp = inb(0x61);
    outb(0x61, tmp & 0xFC);
}

void speaker_beep(uint32_t freq, uint32_t ms) {
    speaker_tone(freq);
    timer_sleep(ms);
    speaker_off();
}

void speaker_boot_melody() {
    // Gateway OS2 chord: C5 E5 G5
    speaker_beep(523, 100);
    timer_sleep(30);
    speaker_beep(659, 100);
    timer_sleep(30);
    speaker_beep(784, 200);
    timer_sleep(50);
}
