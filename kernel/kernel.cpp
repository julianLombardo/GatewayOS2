#include "gdt.h"
#include "idt.h"
#include "timer.h"
#include "panic.h"
#include "../drivers/vga_text.h"
#include "../drivers/serial.h"
#include "../drivers/keyboard.h"
#include "../drivers/mouse.h"
#include "../drivers/framebuffer.h"
#include "../drivers/speaker.h"
#include "../drivers/ports.h"
#include "../memory/pmm.h"
#include "../memory/heap.h"
#include "../gui/font.h"
#include "../gui/theme.h"
#include "../gui/window.h"
#include "../gui/dock.h"
#include "../gui/menu.h"
#include "../gui/desktop.h"
#include "../lib/printf.h"
#include "../lib/string.h"
#include "../lib/math.h"
#include "../drivers/e1000.h"
#include "../net/net.h"
#include "../drivers/ata.h"
#include "nvstore.h"
#include "clipboard.h"

#define MULTIBOOT_MAGIC 0x2BADB002

// Multiboot info structure
struct MultibootInfo {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    // VBE info
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
    // Framebuffer info (multiboot flag bit 12)
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
} PACKED;

extern "C" uint32_t _kernel_end;

// ============================================================
// INTRO SEQUENCE (preserved from Gateway OS v1)
// ============================================================

static uint32_t intro_rand_state = 77713;
static uint32_t intro_rand() {
    intro_rand_state = intro_rand_state * 1103515245 + 12345;
    return (intro_rand_state >> 16) & 0x7FFF;
}

static char intro_random_char() {
    int r = intro_rand() % 62;
    if (r < 10) return '0' + r;
    if (r < 36) return 'A' + (r - 10);
    return 'a' + (r - 36);
}

static void intro_draw_centered(int y, const char* text, uint32_t fg, uint32_t bg, int size) {
    int len = strlen(text);
    int w = len * font_char_width(size);
    int x = (SCREEN_WIDTH - w) / 2;
    font_draw_string(x, y, text, fg, bg, size);
}

static void intro_draw_centered_nobg(int y, const char* text, uint32_t fg, int size) {
    int len = strlen(text);
    int w = len * font_char_width(size);
    int x = (SCREEN_WIDTH - w) / 2;
    font_draw_string_nobg(x, y, text, fg, size);
}

// Amber colors for intro (Gateway aesthetic)
#define INTRO_AMBER     RGB(255, 187, 51)
#define INTRO_BRIGHT    RGB(255, 221, 120)
#define INTRO_DARK_AMB  RGB(120, 80, 20)
#define INTRO_GOLD      RGB(255, 200, 0)
#define INTRO_BLACK     RGB(0, 0, 0)

// Stored credentials from login (available to mail app etc.)
static char g_login_user[32] = {0};
static char g_login_email[64] = {0};
static char g_login_pass[32] = {0};

// Login screen with 3 fields: Username, Email, Password
// Loads saved credentials from disk if available, offers to save on login
static void login_screen() {
    // Try to load saved credentials
    UserCredentials saved;
    bool has_saved = nvstore_load(&saved);

    char username[32] = {0};
    char email[64] = {0};
    char password[32] = {0};
    int user_len = 0, email_len = 0, pass_len = 0;

    // Auto-fill from saved credentials
    if (has_saved) {
        strcpy(username, saved.username);
        user_len = strlen(username);
        strcpy(email, saved.email);
        email_len = strlen(email);
        strcpy(password, saved.password);
        pass_len = strlen(password);
    }

    int bw = 440, bh = 340;
    int bx = (SCREEN_WIDTH - bw) / 2;
    int by = (SCREEN_HEIGHT - bh) / 2;
    int field = 0; // 0=username, 1=email, 2=password
    bool login_ok = false;

    while (!login_ok) {
        fb_clear(INTRO_BLACK);

        // Border frame
        fb_rect(bx, by, bw, bh, INTRO_AMBER);
        fb_rect(bx + 2, by + 2, bw - 4, bh - 4, INTRO_DARK_AMB);

        // Header
        fb_fillrect(bx + 3, by + 3, bw - 6, 26, INTRO_AMBER);
        intro_draw_centered(by + 8, "THE GATEWAY - LOGIN", INTRO_BLACK, INTRO_AMBER, FONT_MEDIUM);

        int fx = bx + 40;
        int fw = bw - 80;

        // Username
        font_draw_string(fx, by + 46, "Username:", INTRO_GOLD, INTRO_BLACK, FONT_MEDIUM);
        fb_fillrect(fx, by + 64, fw, 24, RGB(30, 30, 30));
        fb_rect(fx, by + 64, fw, 24, field == 0 ? INTRO_BRIGHT : INTRO_AMBER);
        fb_fillrect(fx + 2, by + 66, fw - 4, 20, RGB(30, 30, 30));
        font_draw_string(fx + 4, by + 70, username, INTRO_BRIGHT, RGB(30, 30, 30), FONT_MEDIUM);

        // Email
        font_draw_string(fx, by + 102, "Email:", INTRO_GOLD, INTRO_BLACK, FONT_MEDIUM);
        fb_fillrect(fx, by + 120, fw, 24, RGB(30, 30, 30));
        fb_rect(fx, by + 120, fw, 24, field == 1 ? INTRO_BRIGHT : INTRO_AMBER);
        fb_fillrect(fx + 2, by + 122, fw - 4, 20, RGB(30, 30, 30));
        font_draw_string(fx + 4, by + 126, email, INTRO_BRIGHT, RGB(30, 30, 30), FONT_MEDIUM);

        // Password
        font_draw_string(fx, by + 158, "Password:", INTRO_GOLD, INTRO_BLACK, FONT_MEDIUM);
        fb_fillrect(fx, by + 176, fw, 24, RGB(30, 30, 30));
        fb_rect(fx, by + 176, fw, 24, field == 2 ? INTRO_BRIGHT : INTRO_AMBER);
        fb_fillrect(fx + 2, by + 178, fw - 4, 20, RGB(30, 30, 30));
        char stars[32] = {0};
        for (int i = 0; i < pass_len && i < 30; i++) stars[i] = '*';
        font_draw_string(fx + 4, by + 182, stars, INTRO_BRIGHT, RGB(30, 30, 30), FONT_MEDIUM);

        // Blinking cursor in active field
        if ((timer_get_ticks() / 50) % 2 == 0) {
            int cy = 0, clen = 0;
            if (field == 0) { cy = by + 70; clen = user_len; }
            else if (field == 1) { cy = by + 126; clen = email_len; }
            else { cy = by + 182; clen = pass_len; }
            font_draw_char(fx + 4 + clen * font_char_width(FONT_MEDIUM), cy, '_', INTRO_BRIGHT, RGB(30, 30, 30), FONT_MEDIUM);
        }

        // Status line
        if (has_saved) {
            font_draw_string(fx, by + 216, "Credentials loaded from disk", RGB(80, 180, 80), INTRO_BLACK, FONT_MEDIUM);
        }
        font_draw_string(fx, by + 236, "TAB: next field   ENTER: login", INTRO_DARK_AMB, INTRO_BLACK, FONT_MEDIUM);

        // Save checkbox hint
        font_draw_string(fx, by + 256, "Credentials will be saved on login", INTRO_DARK_AMB, INTRO_BLACK, FONT_MEDIUM);

        fb_flip();

        if (keyboard_has_key()) {
            uint8_t key = keyboard_read_key();

            if (key == KEY_TAB) {
                field = (field + 1) % 3;
            } else if (key == KEY_ENTER) {
                if (field < 2) {
                    field++;
                } else if (user_len > 0) {
                    login_ok = true;
                }
            } else if (key == KEY_BACKSPACE) {
                if (field == 0 && user_len > 0) username[--user_len] = 0;
                else if (field == 1 && email_len > 0) email[--email_len] = 0;
                else if (field == 2 && pass_len > 0) password[--pass_len] = 0;
            } else if (key >= 0x20 && key < 0x7F) {
                if (field == 0 && user_len < 30) {
                    username[user_len++] = (char)key;
                    username[user_len] = 0;
                } else if (field == 1 && email_len < 62) {
                    email[email_len++] = (char)key;
                    email[email_len] = 0;
                } else if (field == 2 && pass_len < 30) {
                    password[pass_len++] = (char)key;
                    password[pass_len] = 0;
                }
            }
        }
        asm volatile("hlt");
    }

    // Save credentials to disk for next boot
    UserCredentials creds;
    memset(&creds, 0, sizeof(creds));
    creds.magic = NVSTORE_MAGIC;
    strcpy(creds.username, username);
    strcpy(creds.email, email);
    strcpy(creds.password, password);
    creds.valid = 1;
    nvstore_save(&creds);

    // Store in globals for mail app etc.
    strcpy(g_login_user, username);
    strcpy(g_login_email, email);
    strcpy(g_login_pass, password);

    // Welcome
    fb_clear(INTRO_BLACK);
    int bx2 = (SCREEN_WIDTH - bw) / 2;
    int by2 = (SCREEN_HEIGHT - bh) / 2;
    fb_rect(bx2, by2, bw, bh, INTRO_AMBER);
    fb_rect(bx2 + 2, by2 + 2, bw - 4, bh - 4, INTRO_DARK_AMB);
    fb_fillrect(bx2 + 3, by2 + 3, bw - 6, 26, INTRO_AMBER);
    intro_draw_centered(by2 + 8, "THE GATEWAY - LOGIN", INTRO_BLACK, INTRO_AMBER, FONT_MEDIUM);

    char welcome[64];
    ksprintf(welcome, "Welcome, %s", username);
    intro_draw_centered(by2 + 140, welcome, INTRO_GOLD, INTRO_BLACK, FONT_MEDIUM);
    if (email_len > 0) {
        intro_draw_centered(by2 + 168, email, INTRO_BRIGHT, INTRO_BLACK, FONT_MEDIUM);
    }
    intro_draw_centered(by2 + 200, "Credentials saved.", RGB(80, 180, 80), INTRO_BLACK, FONT_MEDIUM);
    fb_flip();
    timer_sleep(1500);
}

// Matrix rain columns
struct MatrixCol {
    int head_y;
    int length;
    int speed;
    int tick;
    bool active;
};

static void intro_screen() {
    fb_clear(INTRO_BLACK);

    // The quote with typewriter effect
    const char* line1 = "Many are called, Few Are Chosen...";
    const char* line2 = "Are you ready?";

    int y1 = SCREEN_HEIGHT / 2 - 40;
    int y2 = y1 + 24;

    // Typewriter line 1
    int x1 = (SCREEN_WIDTH - strlen(line1) * font_char_width(FONT_MEDIUM)) / 2;
    for (int i = 0; line1[i]; i++) {
        font_draw_char(x1 + i * font_char_width(FONT_MEDIUM), y1, line1[i], INTRO_BRIGHT, INTRO_BLACK, FONT_MEDIUM);
        fb_flip();
        timer_sleep(40);
    }
    timer_sleep(300);

    // Typewriter line 2
    int x2 = (SCREEN_WIDTH - strlen(line2) * font_char_width(FONT_MEDIUM)) / 2;
    for (int i = 0; line2[i]; i++) {
        font_draw_char(x2 + i * font_char_width(FONT_MEDIUM), y2, line2[i], INTRO_BRIGHT, INTRO_BLACK, FONT_MEDIUM);
        fb_flip();
        timer_sleep(40);
    }
    timer_sleep(200);

    // Yes/No prompt
    intro_draw_centered(y2 + 50, "[Y]es    [N]o", INTRO_GOLD, INTRO_BLACK, FONT_MEDIUM);
    fb_flip();

    while (1) {
        if (keyboard_has_key()) {
            uint8_t key = keyboard_read_key();
            if (key == 'y' || key == 'Y') break;
            if (key == 'n' || key == 'N') {
                fb_clear(INTRO_BLACK);
                intro_draw_centered(SCREEN_HEIGHT / 2, "Maybe next time...", INTRO_DARK_AMB, INTRO_BLACK, FONT_MEDIUM);
                fb_flip();
                timer_sleep(2000);
                asm volatile("cli; hlt");
                while(1);
            }
        }
        asm volatile("hlt");
    }

    // --- MATRIX RAIN (AMBER) ---
    fb_clear(INTRO_BLACK);
    fb_flip();

    int cols = SCREEN_WIDTH / 8;
    int rows = SCREEN_HEIGHT / 8;
    MatrixCol mcols[128]; // Max columns
    if (cols > 128) cols = 128;

    for (int i = 0; i < cols; i++) {
        mcols[i].head_y = -(int)(intro_rand() % (rows + 10));
        mcols[i].length = 4 + intro_rand() % 12;
        mcols[i].speed = 1 + intro_rand() % 3;
        mcols[i].tick = 0;
        mcols[i].active = true;
    }

    // Matrix characters buffer
    static char matrix_chars[128][96];
    memset(matrix_chars, 0, sizeof(matrix_chars));

    uint32_t start_tick = timer_get_ticks();
    while (timer_get_ticks() - start_tick < 400) {
        fb_clear(INTRO_BLACK);

        for (int c = 0; c < cols; c++) {
            if (!mcols[c].active) continue;
            mcols[c].tick++;
            if (mcols[c].tick >= mcols[c].speed) {
                mcols[c].tick = 0;
                mcols[c].head_y++;
                if (mcols[c].head_y - mcols[c].length > rows) {
                    mcols[c].head_y = -(int)(intro_rand() % 8);
                    mcols[c].length = 4 + intro_rand() % 12;
                    mcols[c].speed = 1 + intro_rand() % 3;
                }
            }
            for (int r = 0; r < rows; r++) {
                int dist = mcols[c].head_y - r;
                if (dist < 0 || dist > mcols[c].length) continue;
                if (intro_rand() % 8 == 0 || matrix_chars[c][r] == 0)
                    matrix_chars[c][r] = intro_random_char();

                uint32_t color;
                if (dist == 0) color = RGB(255, 255, 200);
                else if (dist <= 2) color = INTRO_BRIGHT;
                else {
                    int fade = 255 - (dist * 200 / mcols[c].length);
                    if (fade < 40) fade = 40;
                    color = RGB(fade, fade * 3 / 4, 0);
                }
                font_draw_char_nobg(c * 8, r * 8, matrix_chars[c][r], color, FONT_SMALL);
            }
        }

        // Overlay title after 2 seconds
        if (timer_get_ticks() - start_tick > 200) {
            intro_draw_centered_nobg(SCREEN_HEIGHT / 2 - 8, "T H E   G A T E W A Y", INTRO_BRIGHT, FONT_LARGE);
        }

        fb_flip();
        asm volatile("hlt");
    }

    // Fade to loading screen
    fb_clear(INTRO_BLACK);
    intro_draw_centered_nobg(SCREEN_HEIGHT / 2 - 30, "T H E   G A T E W A Y", INTRO_BRIGHT, FONT_LARGE);
    fb_flip();
    timer_sleep(500);

    // --- LOADING BAR ---
    fb_clear(INTRO_BLACK);
    intro_draw_centered_nobg(SCREEN_HEIGHT / 2 - 40, "T H E   G A T E W A Y", INTRO_BRIGHT, FONT_LARGE);
    intro_draw_centered(SCREEN_HEIGHT / 2 - 10, "Loading system...", INTRO_DARK_AMB, INTRO_BLACK, FONT_MEDIUM);

    int bar_w = 400;
    int bar_h = 16;
    int bar_x = (SCREEN_WIDTH - bar_w) / 2;
    int bar_y = SCREEN_HEIGHT / 2 + 20;

    fb_rect(bar_x, bar_y, bar_w, bar_h, INTRO_AMBER);
    fb_flip();

    uint32_t load_start = timer_get_ticks();
    uint32_t load_duration = 500;
    int last_fill = 0;

    while (1) {
        uint32_t elapsed = timer_get_ticks() - load_start;
        if (elapsed >= load_duration) break;

        int fill = (int)((elapsed * (bar_w - 2)) / load_duration);
        if (fill > bar_w - 2) fill = bar_w - 2;

        if (fill > last_fill) {
            fb_fillrect(bar_x + 1, bar_y + 1, fill, bar_h - 2, INTRO_BRIGHT);

            char pct[8];
            int p = (int)(elapsed * 100 / load_duration);
            if (p > 100) p = 100;
            ksprintf(pct, "%d%%", p);
            int px = (SCREEN_WIDTH - strlen(pct) * font_char_width(FONT_MEDIUM)) / 2;
            font_draw_string(px, bar_y + 2, pct, INTRO_BLACK, INTRO_BRIGHT, FONT_MEDIUM);
            fb_flip();
            last_fill = fill;
        }
        asm volatile("hlt");
    }

    // 100%
    fb_fillrect(bar_x + 1, bar_y + 1, bar_w - 2, bar_h - 2, INTRO_BRIGHT);
    font_draw_string((SCREEN_WIDTH - 4 * font_char_width(FONT_MEDIUM)) / 2, bar_y + 2,
                     "100%", INTRO_BLACK, INTRO_BRIGHT, FONT_MEDIUM);
    fb_flip();
    timer_sleep(300);

    fb_clear(INTRO_BLACK);
    fb_flip();
    timer_sleep(200);
}

// Shutdown screen
void shutdown_screen() {
    fb_clear(INTRO_BLACK);
    intro_draw_centered_nobg(SCREEN_HEIGHT / 2 - 20, "T H E   G A T E W A Y", INTRO_BRIGHT, FONT_LARGE);
    intro_draw_centered(SCREEN_HEIGHT / 2 + 10, "Shutting down...", INTRO_DARK_AMB, INTRO_BLACK, FONT_MEDIUM);
    fb_flip();
    timer_sleep(1000);

    intro_draw_centered(SCREEN_HEIGHT / 2 + 30, "It is safe to power off.", INTRO_GOLD, INTRO_BLACK, FONT_MEDIUM);
    fb_flip();

    // ACPI shutdown (QEMU/VBox)
    outw(0x604, 0x2000);
    outw(0x4004, 0x3400);

    asm volatile("cli");
    while (1) asm volatile("hlt");
}

// App launchers are now in apps/ directory
// Forward declarations for dock setup
extern "C" {
    void app_launch_workspace();
    void app_launch_terminal();
    void app_launch_edit();
    void app_launch_mail();
    void app_launch_calculator();
    void app_launch_chess();
    void app_launch_preferences();
    void app_launch_help();
    void app_launch_sysmon();
    void app_launch_notes();
    void app_launch_paint();
    void app_launch_gmail();
    void app_launch_javaide();
}

// ============================================================
// KERNEL MAIN
// ============================================================

static void boot_splash() {
    terminal_setcolor(VGA_YELLOW, VGA_BLACK);
    terminal_clear();
    terminal_set_cursor(20, 8);
    terminal_write("================================");
    terminal_set_cursor(20, 9);
    terminal_write("                                ");
    terminal_set_cursor(20, 10);
    terminal_write("      T H E   G A T E W A Y     ");
    terminal_set_cursor(20, 11);
    terminal_write("                                ");
    terminal_set_cursor(20, 12);
    terminal_write("         OS2  Version 2.0       ");
    terminal_set_cursor(20, 13);
    terminal_write("                                ");
    terminal_set_cursor(20, 14);
    terminal_write("================================");
    terminal_set_cursor(25, 18);
    terminal_write("Initializing system...");
}

extern "C" void kmain(uint32_t magic, uint32_t mboot_ptr) {
    // Phase 1: Serial + Text output
    serial_init();
    terminal_init();
    boot_splash();

    if (magic != MULTIBOOT_MAGIC)
        kernel_panic("Not booted by multiboot loader!");

    MultibootInfo* mboot = (MultibootInfo*)mboot_ptr;
    uint32_t mem_kb = mboot->mem_upper + 1024;

    serial_write("\n[GW2] Gateway OS2 booting...\n");

    // Phase 2: CPU tables
    terminal_set_cursor(25, 19);
    terminal_write("Setting up GDT...");
    gdt_init();
    serial_write("[GW2] GDT initialized\n");

    terminal_set_cursor(25, 19);
    terminal_write("Setting up IDT...");
    idt_init();
    isr_init();
    irq_init();
    serial_write("[GW2] IDT/ISR/IRQ initialized\n");

    // Phase 3: Timer + Keyboard
    terminal_set_cursor(25, 19);
    terminal_write("Setting up devices... ");
    timer_init(100);
    keyboard_init();
    serial_write("[GW2] Timer + Keyboard initialized\n");

    asm volatile("sti");

    // Phase 4: Memory
    terminal_set_cursor(25, 19);
    terminal_write("Setting up memory...  ");
    uint32_t kernel_end = (uint32_t)&_kernel_end;
    pmm_init(mem_kb, kernel_end);
    serial_write("[GW2] PMM initialized\n");

    uint32_t heap_start = (kernel_end + 0xFFF) & ~0xFFF;
    heap_start += 0x100000; // Skip past initial structures
    uint32_t heap_size = 4 * 1024 * 1024; // 4 MB heap (bigger for 32bpp)
    heap_init(heap_start, heap_size);
    serial_write("[GW2] Heap initialized (4 MB)\n");

    // Phase 5: Mouse
    terminal_set_cursor(25, 19);
    terminal_write("Setting up mouse...   ");
    mouse_init();
    serial_write("[GW2] Mouse initialized\n");

    // Phase 5b: Network
    terminal_write("Initializing network...");
    if (e1000_init()) {
        net_init();
        terminal_write(" E1000 OK\n");
        terminal_write("Requesting DHCP...    ");
        net_dhcp_discover();
        // Wait for DHCP response
        for (int i = 0; i < 2000; i++) {
            net_poll();
            if (net_get_config()->configured) break;
            for (volatile int j = 0; j < 5000; j++);
        }
        if (net_get_config()->configured) {
            NetConfig* nc = net_get_config();
            char ip_buf[32];
            ksprintf(ip_buf, " IP: %d.%d.%d.%d\n",
                     nc->ip & 0xFF, (nc->ip >> 8) & 0xFF,
                     (nc->ip >> 16) & 0xFF, (nc->ip >> 24) & 0xFF);
            terminal_write(ip_buf);
        } else {
            terminal_write(" No DHCP response\n");
        }
    } else {
        terminal_write(" No NIC found\n");
    }
    serial_write("[GW2] Network phase done\n");

    // Phase 5c: Persistent storage (ATA slave drive for userdata)
    terminal_write("Initializing storage...");
    nvstore_init();
    serial_write("[GW2] Storage phase done\n");

    timer_sleep(500);

    // Phase 6: Switch to VESA framebuffer
    serial_write("[GW2] Setting up VESA framebuffer\n");

    // Check if GRUB gave us a framebuffer
    uint32_t* fb_addr = NULL;
    int fb_w = SCREEN_WIDTH, fb_h = SCREEN_HEIGHT, fb_pitch_bytes = SCREEN_WIDTH * 4;

    if (mboot->flags & (1 << 12)) {
        // Framebuffer info available
        fb_addr = (uint32_t*)(uint32_t)mboot->framebuffer_addr;
        fb_w = mboot->framebuffer_width;
        fb_h = mboot->framebuffer_height;
        fb_pitch_bytes = mboot->framebuffer_pitch;

        char buf[80];
        ksprintf(buf, "[GW2] Framebuffer: %dx%d bpp=%d addr=%p\n",
                fb_w, fb_h, mboot->framebuffer_bpp, fb_addr);
        serial_write(buf);
    } else {
        serial_write("[GW2] WARNING: No framebuffer from GRUB, using fallback\n");
        // Fallback: try Bochs VBE (QEMU -vga std)
        outw(0x1CE, 0x04); outw(0x1CF, 0x00); // Disable VBE first
        outw(0x1CE, 0x01); outw(0x1CF, 1024); // Width
        outw(0x1CE, 0x02); outw(0x1CF, 768);  // Height
        outw(0x1CE, 0x03); outw(0x1CF, 32);   // BPP
        outw(0x1CE, 0x04); outw(0x1CF, 0x41); // Enable VBE + LFB

        // Read LFB address from PCI BAR0 of VGA device (bus 0, dev 2, func 0)
        // PCI config address: 0x80000000 | (bus<<16) | (dev<<11) | (func<<8) | (reg & 0xFC)
        // BAR0 is at offset 0x10
        uint32_t pci_addr = 0x80000000 | (0 << 16) | (2 << 11) | (0 << 8) | 0x10;
        outl(0xCF8, pci_addr);
        uint32_t bar0 = inl(0xCFC) & 0xFFFFFFF0; // Mask lower bits

        if (bar0 == 0 || bar0 == 0xFFFFFFF0) {
            // Fallback to common QEMU stdvga address
            bar0 = 0xFD000000;
            serial_write("[GW2] PCI BAR0 read failed, using 0xFD000000\n");
        }

        fb_addr = (uint32_t*)bar0;
        fb_w = 1024;
        fb_h = 768;
        fb_pitch_bytes = 1024 * 4;

        char buf2[80];
        ksprintf(buf2, "[GW2] Bochs VBE fallback: LFB at %p\n", fb_addr);
        serial_write(buf2);
    }

    fb_init(fb_addr, fb_w, fb_h, fb_pitch_bytes);
    font_init();

    // Login screen
    login_screen();

    // Intro sequence (quote, matrix, loading bar)
    intro_screen();
    speaker_boot_melody();

    // Phase 7: Window manager + desktop
    wm_init();
    dock_init();
    menu_init();
    clipboard_init();
    desktop_init();

    // Setup dock
    dock_add_item("Files", app_launch_workspace, NULL);
    dock_add_item("Terminal", app_launch_terminal, NULL);
    dock_add_item("Edit", app_launch_edit, NULL);
    dock_add_item("Java", app_launch_javaide, NULL);
    dock_add_item("Gmail", app_launch_gmail, NULL);
    dock_add_separator();
    dock_add_item("Calc", app_launch_calculator, NULL);
    dock_add_item("SysMon", app_launch_sysmon, NULL);
    dock_add_item("Notes", app_launch_notes, NULL);
    dock_add_separator();
    dock_add_item("Chess", app_launch_chess, NULL);
    dock_add_item("Paint", app_launch_paint, NULL);
    dock_add_separator();
    dock_add_item("Prefs", app_launch_preferences, NULL);

    // Setup main menu
    menu_setup_main_menu();

    serial_write("[GW2] Desktop environment ready\n");

    // Launch help on first boot
    app_launch_help();

    // ============================================================
    // MAIN EVENT LOOP
    // ============================================================
    bool prev_left = false;
    bool prev_right = false;

    while (1) {
        // Keyboard
        while (keyboard_has_key()) {
            uint8_t key = keyboard_read_key();
            desktop_handle_key(key);
        }

        // Mouse
        bool left = mouse_left_button();
        bool right = mouse_right_button();
        int mx = mouse_get_x();
        int my = mouse_get_y();

        if ((left && !prev_left) || (right && !prev_right)) {
            desktop_handle_mouse(mx, my, left, right);
        }
        prev_left = left;
        prev_right = right;

        desktop_handle_mouse_move(mx, my, left);

        // Network polling
        net_poll();

        if (desktop_shutdown_requested()) {
            shutdown_screen();
        }

        // Render
        desktop_draw();

        asm volatile("hlt");
    }
}
