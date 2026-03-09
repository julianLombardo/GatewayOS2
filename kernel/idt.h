#pragma once

#include "../lib/types.h"

struct Registers {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;
} PACKED;

typedef void (*isr_handler_t)(Registers*);
typedef void (*irq_handler_t)(Registers*);

void idt_init();
void isr_init();
void irq_init();
void irq_install_handler(int irq, irq_handler_t handler);
void irq_uninstall_handler(int irq);
