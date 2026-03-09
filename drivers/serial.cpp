#include "serial.h"
#include "ports.h"

#define COM1 0x3F8

void serial_init() {
    outb(COM1 + 1, 0x00);  // Disable interrupts
    outb(COM1 + 3, 0x80);  // Enable DLAB
    outb(COM1 + 0, 0x03);  // Baud 38400
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);  // 8N1
    outb(COM1 + 2, 0xC7);  // Enable FIFO
    outb(COM1 + 4, 0x0B);  // IRQs enabled, RTS/DSR set
}

void serial_write_char(char c) {
    while ((inb(COM1 + 5) & 0x20) == 0);
    outb(COM1, c);
}

void serial_write(const char* str) {
    while (*str) {
        if (*str == '\n') serial_write_char('\r');
        serial_write_char(*str++);
    }
}

