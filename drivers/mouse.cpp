#include "mouse.h"
#include "../kernel/idt.h"
#include "../drivers/framebuffer.h"
#include "ports.h"

static int mouse_x = SCREEN_WIDTH / 2;
static int mouse_y = SCREEN_HEIGHT / 2;
static bool btn_left = false, btn_right = false, btn_middle = false;
static uint8_t mouse_cycle = 0;
static int8_t mouse_bytes[3];

static void mouse_wait_write() {
    int timeout = 100000;
    while (timeout-- > 0) {
        if ((inb(0x64) & 2) == 0) return;
    }
}

static void mouse_wait_read() {
    int timeout = 100000;
    while (timeout-- > 0) {
        if ((inb(0x64) & 1) != 0) return;
    }
}

static void mouse_write(uint8_t data) {
    mouse_wait_write();
    outb(0x64, 0xD4);
    mouse_wait_write();
    outb(0x60, data);
}

static void mouse_callback(Registers* regs) {
    (void)regs;
    uint8_t status = inb(0x64);
    if (!(status & 0x20)) return;

    int8_t data = (int8_t)inb(0x60);

    switch (mouse_cycle) {
        case 0:
            mouse_bytes[0] = data;
            if (data & 0x08) mouse_cycle = 1; // Bit 3 must be set
            break;
        case 1:
            mouse_bytes[1] = data;
            mouse_cycle = 2;
            break;
        case 2:
            mouse_bytes[2] = data;
            mouse_cycle = 0;

            btn_left   = (mouse_bytes[0] & 0x01) != 0;
            btn_right  = (mouse_bytes[0] & 0x02) != 0;
            btn_middle = (mouse_bytes[0] & 0x04) != 0;

            mouse_x += mouse_bytes[1];
            mouse_y -= mouse_bytes[2];

            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x >= SCREEN_WIDTH) mouse_x = SCREEN_WIDTH - 1;
            if (mouse_y >= SCREEN_HEIGHT) mouse_y = SCREEN_HEIGHT - 1;
            break;
    }
}

void mouse_init() {
    // Enable auxiliary mouse device
    mouse_wait_write();
    outb(0x64, 0xA8);

    // Enable interrupts
    mouse_wait_write();
    outb(0x64, 0x20);
    mouse_wait_read();
    uint8_t status = inb(0x60) | 2;
    mouse_wait_write();
    outb(0x64, 0x60);
    mouse_wait_write();
    outb(0x60, status);

    // Use default settings
    mouse_write(0xF6);
    mouse_wait_read();
    inb(0x60);

    // Enable data reporting
    mouse_write(0xF4);
    mouse_wait_read();
    inb(0x60);

    irq_install_handler(12, mouse_callback);
}

int mouse_get_x() { return mouse_x; }
int mouse_get_y() { return mouse_y; }
bool mouse_left_button() { return btn_left; }
bool mouse_right_button() { return btn_right; }
bool mouse_middle_button() { return btn_middle; }
