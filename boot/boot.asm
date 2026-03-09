; Gateway OS2 - Multiboot boot stub with VESA framebuffer request
; Requests 1024x768x32 linear framebuffer from GRUB

BITS 32

; Multiboot header constants
MBALIGN     equ 1 << 0          ; Align modules on page boundaries
MEMINFO     equ 1 << 1          ; Provide memory map
VIDEO_MODE  equ 1 << 2          ; Video mode info requested
FLAGS       equ MBALIGN | MEMINFO | VIDEO_MODE
MAGIC       equ 0x1BADB002
CHECKSUM    equ -(MAGIC + FLAGS)

; Multiboot header
section .multiboot
align 4
    dd MAGIC
    dd FLAGS
    dd CHECKSUM
    ; a.out kludge fields (zeros for ELF)
    dd 0    ; header_addr
    dd 0    ; load_addr
    dd 0    ; load_end_addr
    dd 0    ; bss_end_addr
    dd 0    ; entry_addr
    ; Video mode fields
    dd 0    ; mode_type: 0 = linear graphics
    dd 1024 ; width
    dd 768  ; height
    dd 32   ; depth (bits per pixel)

; Stack
section .bss
align 16
stack_bottom:
    resb 65536      ; 64 KB stack
stack_top:

; Entry point
section .text
global _start
extern kmain

_start:
    mov esp, stack_top

    ; Push multiboot info pointer and magic number
    push ebx        ; Multiboot info structure pointer
    push eax        ; Multiboot magic number

    call kmain

    ; Should never return
    cli
.hang:
    hlt
    jmp .hang

; GDT flush (called from C)
global gdt_flush
gdt_flush:
    mov eax, [esp + 4]
    lgdt [eax]
    mov ax, 0x10    ; Data segment selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.flush ; Code segment selector
.flush:
    ret

; IDT load (called from C)
global idt_load
idt_load:
    mov eax, [esp + 4]
    lidt [eax]
    ret

; ISR stubs
%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    push dword 0
    push dword %1
    jmp isr_common
%endmacro

%macro ISR_ERRCODE 1
global isr%1
isr%1:
    push dword %1
    jmp isr_common
%endmacro

ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7
ISR_ERRCODE   8
ISR_NOERRCODE 9
ISR_ERRCODE   10
ISR_ERRCODE   11
ISR_ERRCODE   12
ISR_ERRCODE   13
ISR_ERRCODE   14
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_ERRCODE   17
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_ERRCODE   30
ISR_NOERRCODE 31

extern isr_handler
isr_common:
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp
    call isr_handler
    add esp, 4
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8     ; Remove error code and ISR number
    iret

; IRQ stubs
%macro IRQ 2
global irq%1
irq%1:
    push dword 0
    push dword %2
    jmp irq_common
%endmacro

IRQ 0,  32
IRQ 1,  33
IRQ 2,  34
IRQ 3,  35
IRQ 4,  36
IRQ 5,  37
IRQ 6,  38
IRQ 7,  39
IRQ 8,  40
IRQ 9,  41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

extern irq_handler
irq_common:
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp
    call irq_handler
    add esp, 4
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8
    iret

; Context switch stub
global context_switch
context_switch:
    ; void context_switch(uint32_t* old_esp, uint32_t new_esp)
    mov eax, [esp + 4]  ; old_esp pointer
    mov edx, [esp + 8]  ; new_esp value
    ; Save callee-saved registers
    push ebx
    push esi
    push edi
    push ebp
    ; Save old stack pointer
    mov [eax], esp
    ; Load new stack pointer
    mov esp, edx
    ; Restore callee-saved registers
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
