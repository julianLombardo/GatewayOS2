#include "timer.h"
#include "idt.h"
#include "../drivers/ports.h"

static volatile uint32_t ticks = 0;
static uint32_t timer_freq = 100;

static void timer_callback(Registers* regs) {
    (void)regs;
    ticks++;
}

void timer_init(uint32_t frequency) {
    timer_freq = frequency;
    irq_install_handler(0, timer_callback);
    uint32_t divisor = 1193180 / frequency;
    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
}

uint32_t timer_get_ticks() {
    return ticks;
}

void timer_sleep(uint32_t ms) {
    uint32_t target = ticks + (ms * timer_freq) / 1000;
    while (ticks < target)
        asm volatile("hlt");
}
