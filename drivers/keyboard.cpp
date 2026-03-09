#include "keyboard.h"
#include "../kernel/idt.h"
#include "ports.h"

#define KB_BUF_SIZE 128

static uint8_t kb_buffer[KB_BUF_SIZE];
static int kb_head = 0, kb_tail = 0;
static bool shift_held = false;
static bool ctrl_held = false;
static bool alt_held = false;
static bool extended = false;

// US QWERTY scancode to ASCII (lowercase)
static const uint8_t scancode_to_ascii[128] = {
    0, KEY_ESCAPE, '1','2','3','4','5','6','7','8','9','0','-','=', KEY_BACKSPACE,
    KEY_TAB, 'q','w','e','r','t','y','u','i','o','p','[',']', KEY_ENTER,
    0, 'a','s','d','f','g','h','j','k','l',';','\'', '`',
    0, '\\','z','x','c','v','b','n','m',',','.','/', 0,
    '*', 0, ' ', 0,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10,
    0, 0,
    0, 0, 0, '-', 0, 0, 0, '+', 0, 0, 0, 0, 0,
    0, 0, 0, KEY_F11, KEY_F12,
};

static const uint8_t scancode_to_ascii_shift[128] = {
    0, KEY_ESCAPE, '!','@','#','$','%','^','&','*','(',')','_','+', KEY_BACKSPACE,
    KEY_TAB, 'Q','W','E','R','T','Y','U','I','O','P','{','}', KEY_ENTER,
    0, 'A','S','D','F','G','H','J','K','L',':','"', '~',
    0, '|','Z','X','C','V','B','N','M','<','>','?', 0,
    '*', 0, ' ', 0,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10,
    0, 0,
    0, 0, 0, '-', 0, 0, 0, '+', 0, 0, 0, 0, 0,
    0, 0, 0, KEY_F11, KEY_F12,
};

static void kb_push(uint8_t key) {
    int next = (kb_head + 1) % KB_BUF_SIZE;
    if (next != kb_tail) {
        kb_buffer[kb_head] = key;
        kb_head = next;
    }
}

static void keyboard_callback(Registers* regs) {
    (void)regs;
    uint8_t scancode = inb(0x60);

    if (scancode == 0xE0) { extended = true; return; }

    if (extended) {
        extended = false;
        bool released = (scancode & 0x80) != 0;
        scancode &= 0x7F;
        if (!released) {
            switch (scancode) {
                case 0x48: kb_push(KEY_UP); break;
                case 0x50: kb_push(KEY_DOWN); break;
                case 0x4B: kb_push(KEY_LEFT); break;
                case 0x4D: kb_push(KEY_RIGHT); break;
                case 0x47: kb_push(KEY_HOME); break;
                case 0x4F: kb_push(KEY_END); break;
                case 0x49: kb_push(KEY_PGUP); break;
                case 0x51: kb_push(KEY_PGDN); break;
                case 0x52: kb_push(KEY_INSERT); break;
                case 0x53: kb_push(KEY_DELETE); break;
            }
        }
        return;
    }

    bool released = (scancode & 0x80) != 0;
    scancode &= 0x7F;

    // Modifier keys
    if (scancode == 0x2A || scancode == 0x36) { shift_held = !released; return; }
    if (scancode == 0x1D) { ctrl_held = !released; return; }
    if (scancode == 0x38) { alt_held = !released; return; }

    if (!released && scancode < 128) {
        uint8_t key = shift_held ? scancode_to_ascii_shift[scancode] : scancode_to_ascii[scancode];
        if (key) kb_push(key);
    }
}

void keyboard_init() {
    irq_install_handler(1, keyboard_callback);
}

bool keyboard_has_key() {
    return kb_head != kb_tail;
}

uint8_t keyboard_read_key() {
    if (kb_head == kb_tail) return 0;
    uint8_t key = kb_buffer[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUF_SIZE;
    return key;
}

bool keyboard_alt_held() { return alt_held; }
bool keyboard_ctrl_held() { return ctrl_held; }
bool keyboard_shift_held() { return shift_held; }
