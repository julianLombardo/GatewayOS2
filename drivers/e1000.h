#pragma once

#include "../lib/types.h"

// E1000 Intel 82540EM (QEMU default NIC)
#define E1000_VENDOR 0x8086
#define E1000_DEVICE 0x100E

// Initialize E1000 NIC. Returns true if found and initialized.
bool e1000_init();

// Send a raw ethernet frame. Returns true on success.
bool e1000_send(const void* data, uint16_t len);

// Receive a packet. Returns length (0 if none available).
uint16_t e1000_recv(void* buf, uint16_t buf_size);

// Get MAC address (6 bytes)
void e1000_get_mac(uint8_t* mac);

// Check if NIC is initialized and link is up
bool e1000_link_up();

// Poll for received packets (call from main loop or IRQ)
void e1000_poll();
