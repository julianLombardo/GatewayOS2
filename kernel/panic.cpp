#include "panic.h"
#include "../drivers/serial.h"
#include "../drivers/vga_text.h"

void kernel_panic(const char* msg) {
    asm volatile("cli");
    serial_write("\n!!! KERNEL PANIC: ");
    serial_write(msg);
    serial_write("\n");

    terminal_setcolor(VGA_WHITE, VGA_RED);
    terminal_clear();
    terminal_set_cursor(30, 10);
    terminal_write("*** KERNEL PANIC ***");
    terminal_set_cursor(20, 12);
    terminal_write(msg);
    terminal_set_cursor(25, 15);
    terminal_write("System halted.");

    while (1) asm volatile("hlt");
}
