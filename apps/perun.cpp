#include "apps.h"
#include "../memory/heap.h"
#include "../memory/pmm.h"
#include "../kernel/pe_loader.h"
#include "../kernel/win32_shim.h"
#include "../drivers/ata.h"
#include "../lib/printf.h"

// ============================================================
// PE RUNNER — Load and execute Win32 PE32 executables
// ============================================================

// PE files are stored on userdata.img starting at sector 16
// Layout:
//   Sector 0:     nvstore credentials
//   Sector 16-17: PE catalog (list of stored EXEs)
//   Sector 32+:   PE file data
//
// Catalog format (sector 16):
//   uint32_t magic = 0x50454341 ("PECA")
//   uint32_t count
//   struct { char name[24]; uint32_t start_sector; uint32_t size_bytes; } entries[15]

#define PE_CATALOG_SECTOR  16
#define PE_DATA_START      32
#define PE_MAX_ENTRIES     15
#define PE_CATALOG_MAGIC   0x50454341

struct PECatalogEntry {
    char     name[24];
    uint32_t start_sector;
    uint32_t size_bytes;
};

struct PECatalog {
    uint32_t magic;
    uint32_t count;
    PECatalogEntry entries[PE_MAX_ENTRIES];
};

// App state
struct PERunState {
    int selected;
    int scroll;
    PECatalog catalog;
    bool catalog_loaded;

    // Execution state
    bool running;
    bool finished;
    int exit_code;
    char status[64];

    // Built-in demo mode (no disk)
    bool demo_mode;
};

// ============================================================
// Built-in minimal PE32 binary (Hello World MessageBox)
// Programmatically built with proper headers, code, imports, and relocations
// ============================================================

// Helper to write a uint16_t/uint32_t at a byte offset
static void w16(uint8_t* base, int off, uint16_t v) { *(uint16_t*)(base+off) = v; }
static void w32(uint8_t* base, int off, uint32_t v) { *(uint32_t*)(base+off) = v; }

static uint8_t* build_demo_pe(uint32_t* out_size) {
    // Layout (FileAlignment=0x200, SectionAlignment=0x1000):
    //   0x000: Headers (DOS+PE+COFF+Optional+4 section headers) = 0x200
    //   0x200: .text   (RVA 0x1000) code
    //   0x400: .rdata  (RVA 0x2000) strings
    //   0x600: .idata  (RVA 0x3000) imports
    //   0x800: .reloc  (RVA 0x4000) base relocations
    // SizeOfImage = 0x5000

    const uint32_t IMAGE_BASE = 0x00400000;
    uint32_t file_size = 0xA00; // 5 * 0x200
    uint8_t* pe = (uint8_t*)kmalloc(file_size);
    if (!pe) return nullptr;
    memset(pe, 0, file_size);

    // === DOS Header (64 bytes @ 0x00) ===
    w16(pe, 0x00, 0x5A4D);      // e_magic = "MZ"
    w32(pe, 0x3C, 0x40);        // e_lfanew -> PE sig at 0x40

    // === PE Signature (4 bytes @ 0x40) ===
    w32(pe, 0x40, 0x00004550);   // "PE\0\0"

    // === COFF Header (20 bytes @ 0x44) ===
    w16(pe, 0x44, 0x014C);      // Machine = i386
    w16(pe, 0x46, 4);           // NumberOfSections = 4
    w16(pe, 0x54, 0xE0);        // SizeOfOptionalHeader = 224 (0xE0)
    w16(pe, 0x56, 0x0102);      // Characteristics = EXECUTABLE|32BIT

    // === Optional Header (224 bytes @ 0x58) ===
    int oh = 0x58;
    w16(pe, oh+0, 0x010B);      // Magic = PE32
    w32(pe, oh+16, 0x1000);     // AddressOfEntryPoint
    w32(pe, oh+20, 0x1000);     // BaseOfCode
    w32(pe, oh+24, 0x2000);     // BaseOfData
    w32(pe, oh+28, IMAGE_BASE); // ImageBase
    w32(pe, oh+32, 0x1000);     // SectionAlignment
    w32(pe, oh+36, 0x200);      // FileAlignment
    w32(pe, oh+56, 0x5000);     // SizeOfImage
    w32(pe, oh+60, 0x200);      // SizeOfHeaders
    w16(pe, oh+68, 3);          // Subsystem = CUI
    w32(pe, oh+72, 0x10000);    // SizeOfStackReserve
    w32(pe, oh+76, 0x1000);     // SizeOfStackCommit
    w32(pe, oh+80, 0x10000);    // SizeOfHeapReserve
    w32(pe, oh+84, 0x1000);     // SizeOfHeapCommit
    w32(pe, oh+92, 16);         // NumberOfRvaAndSizes = 16

    // DataDirectory (16 entries * 8 bytes = 128 bytes @ oh+96)
    int dd = oh + 96;
    // [1] Import Directory
    w32(pe, dd + 1*8, 0x3000);  // RVA
    w32(pe, dd + 1*8+4, 0x100); // Size
    // [5] Base Relocation
    w32(pe, dd + 5*8, 0x4000);  // RVA
    w32(pe, dd + 5*8+4, 0x1C);  // Size (one block: 8 header + 4*2 entries + 2*2 padding = 0x1C)

    // === Section Headers (4 * 40 bytes @ 0x138) ===
    int sh = 0x138;

    // .text
    memcpy(pe+sh, ".text\0\0\0", 8);
    w32(pe, sh+8,  0x40);       // VirtualSize
    w32(pe, sh+12, 0x1000);     // VirtualAddress
    w32(pe, sh+16, 0x200);      // SizeOfRawData
    w32(pe, sh+20, 0x200);      // PointerToRawData
    w32(pe, sh+36, 0x60000020); // CODE|EXECUTE|READ

    // .rdata
    sh += 40;
    memcpy(pe+sh, ".rdata\0\0", 8);
    w32(pe, sh+8,  0x40);
    w32(pe, sh+12, 0x2000);
    w32(pe, sh+16, 0x200);
    w32(pe, sh+20, 0x400);
    w32(pe, sh+36, 0x40000040); // INITIALIZED_DATA|READ

    // .idata
    sh += 40;
    memcpy(pe+sh, ".idata\0\0", 8);
    w32(pe, sh+8,  0x100);
    w32(pe, sh+12, 0x3000);
    w32(pe, sh+16, 0x200);
    w32(pe, sh+20, 0x600);
    w32(pe, sh+36, 0xC0000040); // INITIALIZED_DATA|READ|WRITE

    // .reloc
    sh += 40;
    memcpy(pe+sh, ".reloc\0\0", 8);
    w32(pe, sh+8,  0x20);
    w32(pe, sh+12, 0x4000);
    w32(pe, sh+16, 0x200);
    w32(pe, sh+20, 0x800);
    w32(pe, sh+36, 0x42000040); // INITIALIZED_DATA|DISCARDABLE|READ

    // === .text @ file 0x200 (RVA 0x1000) ===
    // Code: MessageBoxA(NULL, msg, title, MB_OK); ExitProcess(0);
    // IAT at RVA 0x3080: [0]=MessageBoxA, [1]=ExitProcess
    uint8_t* code = pe + 0x200;
    int ci = 0;

    // push 0 (MB_OK)
    code[ci++] = 0x6A; code[ci++] = 0x00;
    // push title_addr (IMAGE_BASE + 0x2000) — needs reloc at RVA 0x1003
    code[ci++] = 0x68;
    w32(code, ci, IMAGE_BASE + 0x2000); ci += 4;
    // push msg_addr (IMAGE_BASE + 0x2010) — needs reloc at RVA 0x1008
    code[ci++] = 0x68;
    w32(code, ci, IMAGE_BASE + 0x2010); ci += 4;
    // push 0 (NULL hwnd)
    code[ci++] = 0x6A; code[ci++] = 0x00;
    // call [IAT MessageBoxA] — needs reloc at RVA 0x1010
    code[ci++] = 0xFF; code[ci++] = 0x15;
    w32(code, ci, IMAGE_BASE + 0x3080); ci += 4;
    // push 0
    code[ci++] = 0x6A; code[ci++] = 0x00;
    // call [IAT ExitProcess] — needs reloc at RVA 0x1018
    code[ci++] = 0xFF; code[ci++] = 0x15;
    w32(code, ci, IMAGE_BASE + 0x3084); ci += 4;
    // ret (safety net)
    code[ci++] = 0xC3;

    // === .rdata @ file 0x400 (RVA 0x2000) ===
    strcpy((char*)(pe + 0x400), "GatewayOS2");       // @ RVA 0x2000
    strcpy((char*)(pe + 0x410), "Hello from Win32 PE!"); // @ RVA 0x2010

    // === .idata @ file 0x600 (RVA 0x3000) ===
    uint8_t* idata = pe + 0x600;

    // Import Directory: 2 entries + null term (3 * 20 bytes)
    // Entry 0: user32.dll (MessageBoxA)
    w32(idata, 0, 0x3060);     // OriginalFirstThunk -> INT
    w32(idata, 16, 0x30A0);    // Name -> "user32.dll"
    w32(idata, 12, 0x3080);    // FirstThunk -> IAT[0] (swapped: Name@16, FirstThunk@12 wrong)

    // Fix: IMAGE_IMPORT_DESCRIPTOR layout:
    //   0: OriginalFirstThunk (4)
    //   4: TimeDateStamp (4)
    //   8: ForwarderChain (4)
    //  12: Name (4)
    //  16: FirstThunk (4)
    memset(idata, 0, 60); // Clear all 3 entries

    // Entry 0: user32.dll
    w32(idata, 0,  0x3060);    // OriginalFirstThunk
    w32(idata, 12, 0x30A0);    // Name
    w32(idata, 16, 0x3080);    // FirstThunk (IAT[0])

    // Entry 1: kernel32.dll
    w32(idata, 20, 0x3068);    // OriginalFirstThunk
    w32(idata, 32, 0x30B0);    // Name
    w32(idata, 36, 0x3084);    // FirstThunk (IAT[1])

    // Entry 2: null terminator (already zero)

    // INT (Import Name Table)
    w32(idata, 0x60, 0x30C0);  // -> "MessageBoxA" hint/name
    w32(idata, 0x64, 0);       // terminator
    w32(idata, 0x68, 0x30D0);  // -> "ExitProcess" hint/name
    w32(idata, 0x6C, 0);       // terminator

    // IAT (Import Address Table) — pre-bound, loader overwrites
    w32(idata, 0x80, 0x30C0);  // -> MessageBoxA (will be patched)
    w32(idata, 0x84, 0x30D0);  // -> ExitProcess (will be patched)

    // DLL name strings
    strcpy((char*)(idata + 0xA0), "user32.dll");
    strcpy((char*)(idata + 0xB0), "kernel32.dll");

    // IMAGE_IMPORT_BY_NAME entries (Hint + Name)
    w16(idata, 0xC0, 0);
    strcpy((char*)(idata + 0xC2), "MessageBoxA");
    w16(idata, 0xD0, 0);
    strcpy((char*)(idata + 0xD2), "ExitProcess");

    // === .reloc @ file 0x800 (RVA 0x4000) ===
    // Base relocation block for page 0x1000 (the .text section)
    // 4 HIGHLOW fixups at offsets 0x003, 0x008, 0x010, 0x016
    uint8_t* reloc = pe + 0x800;
    w32(reloc, 0, 0x1000);     // VirtualAddress (page RVA)
    w32(reloc, 4, 0x14);       // SizeOfBlock = 8 + 4*2 + 2*0 padding = 16...
    // Actually: header(8) + 4 entries(8) = 16 = 0x10, but must be 4-byte aligned
    // Let's use 5 entries (4 real + 1 ABSOLUTE padding) = 8 + 10 = 18 -> pad to 20 = 0x14
    w32(reloc, 4, 0x14);       // SizeOfBlock = 20

    // TypeOffset entries (each uint16_t): type(4 bits) | offset(12 bits)
    // IMAGE_REL_BASED_HIGHLOW = 3
    w16(reloc, 8,  (3 << 12) | 0x003);  // push title addr
    w16(reloc, 10, (3 << 12) | 0x008);  // push msg addr
    w16(reloc, 12, (3 << 12) | 0x010);  // call [IAT MessageBoxA]
    w16(reloc, 14, (3 << 12) | 0x018);  // call [IAT ExitProcess]
    w16(reloc, 16, (0 << 12) | 0x000);  // ABSOLUTE padding
    w16(reloc, 18, (0 << 12) | 0x000);  // ABSOLUTE padding

    *out_size = file_size;
    return pe;
}

// ============================================================
// PE Runner App Drawing
// ============================================================

static void perun_draw(Window* win, int cx, int cy, int cw, int ch) {
    PERunState* st = (PERunState*)win->userdata;
    if (!st) return;

    // Dark themed like a dev tool
    fb_fillrect(cx, cy, cw, ch, RGB(25, 25, 35));

    // Title bar area
    fb_fillrect(cx, cy, cw, 24, RGB(40, 40, 60));
    font_draw_string(cx + 8, cy + 6, "PE32 Loader", NX_AMBER, RGB(40, 40, 60), FONT_MEDIUM);
    font_draw_string(cx + cw - 100, cy + 8, "Win32 Shim", RGB(100, 100, 140), RGB(40, 40, 60), FONT_SMALL);
    fb_hline(cx, cy + 24, cw, RGB(80, 80, 120));

    int y = cy + 30;

    if (st->finished) {
        // Show execution results
        font_draw_string(cx + 8, y, "Execution Complete", NX_GREEN, RGB(25, 25, 35), FONT_MEDIUM);
        y += 18;

        char buf[64];
        ksprintf(buf, "Exit code: %d", st->exit_code);
        font_draw_string(cx + 8, y, buf, NX_LTGRAY, RGB(25, 25, 35), FONT_SMALL);
        y += 16;

        // Show MessageBox result if any
        if (w32_msgbox_active) {
            fb_fillrect(cx + 8, y, cw - 16, 60, RGB(45, 45, 65));
            fb_rect(cx + 8, y, cw - 16, 60, RGB(80, 80, 120));

            font_draw_string(cx + 14, y + 4, w32_msgbox_title, NX_WHITE, RGB(45, 45, 65), FONT_MEDIUM);
            fb_hline(cx + 12, y + 20, cw - 24, RGB(60, 60, 90));
            font_draw_string(cx + 14, y + 26, w32_msgbox_text, NX_LTGRAY, RGB(45, 45, 65), FONT_SMALL);

            // OK button
            nx_draw_button(cx + cw/2 - 30, y + 42, 50, 16, "OK", false, true);
            y += 66;
        }

        // Console output
        if (w32_console_len > 0) {
            y += 4;
            font_draw_string(cx + 8, y, "Console Output:", RGB(100, 200, 100), RGB(25, 25, 35), FONT_SMALL);
            y += 14;

            // Draw console area
            fb_fillrect(cx + 8, y, cw - 16, ch - (y - cy) - 30, RGB(0, 0, 0));
            fb_rect(cx + 8, y, cw - 16, ch - (y - cy) - 30, RGB(50, 50, 70));

            int text_y = y + 4;
            int line_start = 0;
            for (int i = 0; i <= w32_console_len; i++) {
                if (i == w32_console_len || w32_console_buf[i] == '\n') {
                    if (i > line_start && text_y < cy + ch - 40) {
                        char line[128];
                        int len = i - line_start;
                        if (len > 127) len = 127;
                        memcpy(line, w32_console_buf + line_start, len);
                        line[len] = 0;
                        font_draw_string(cx + 12, text_y, line, NX_GREEN, NX_BLACK, FONT_SMALL);
                        text_y += 11;
                    }
                    line_start = i + 1;
                }
            }
        }

        // Bottom controls
        nx_draw_button(cx + 8, cy + ch - 22, 80, 18, "Run Again", false, false);
        nx_draw_button(cx + 96, cy + ch - 22, 60, 18, "Back", false, false);

    } else if (st->running) {
        font_draw_string(cx + 8, y, "Running...", NX_AMBER, RGB(25, 25, 35), FONT_MEDIUM);
        y += 18;
        font_draw_string(cx + 8, y, st->status, NX_LTGRAY, RGB(25, 25, 35), FONT_SMALL);

    } else {
        // Main menu: list loadable PEs
        font_draw_string(cx + 8, y, "Available Programs:", NX_WHITE, RGB(25, 25, 35), FONT_SMALL);
        y += 16;

        // Built-in demo
        bool sel = (st->selected == 0);
        if (sel) fb_fillrect(cx + 4, y - 1, cw - 8, 16, RGB(50, 50, 80));
        font_draw_string(cx + 12, y, "[Built-in] Hello World", sel ? NX_AMBER : NX_LTGRAY, sel ? RGB(50,50,80) : RGB(25,25,35), FONT_SMALL);
        font_draw_string(cx + cw - 70, y, "PE32/CUI", RGB(80, 80, 110), sel ? RGB(50,50,80) : RGB(25,25,35), FONT_SMALL);
        y += 16;

        // Catalog entries from disk
        if (st->catalog_loaded) {
            for (uint32_t i = 0; i < st->catalog.count && i < PE_MAX_ENTRIES; i++) {
                sel = (st->selected == (int)(i + 1));
                if (sel) fb_fillrect(cx + 4, y - 1, cw - 8, 16, RGB(50, 50, 80));

                font_draw_string(cx + 12, y, st->catalog.entries[i].name,
                                 sel ? NX_AMBER : NX_LTGRAY,
                                 sel ? RGB(50,50,80) : RGB(25,25,35), FONT_SMALL);

                char szbuf[16];
                ksprintf(szbuf, "%d B", st->catalog.entries[i].size_bytes);
                font_draw_string(cx + cw - 70, y, szbuf, RGB(80, 80, 110),
                                 sel ? RGB(50,50,80) : RGB(25,25,35), FONT_SMALL);
                y += 16;
            }
        }

        // Info section
        y += 8;
        fb_hline(cx + 8, y, cw - 16, RGB(50, 50, 70));
        y += 8;

        font_draw_string(cx + 8, y, "PE32 Loader v1.0", RGB(80, 80, 110), RGB(25, 25, 35), FONT_SMALL);
        y += 12;
        font_draw_string(cx + 8, y, "Supports: i386 PE32, CUI/GUI subsystem", RGB(60, 60, 90), RGB(25, 25, 35), FONT_SMALL);
        y += 12;
        font_draw_string(cx + 8, y, "Win32 shim: kernel32 + user32 + msvcrt", RGB(60, 60, 90), RGB(25, 25, 35), FONT_SMALL);
        y += 12;
        font_draw_string(cx + 8, y, "Imports: MessageBoxA, ExitProcess, etc.", RGB(60, 60, 90), RGB(25, 25, 35), FONT_SMALL);

        // Bottom controls
        nx_draw_button(cx + 8, cy + ch - 22, 60, 18, "Run", false, true);
        font_draw_string(cx + 80, cy + ch - 18, "Enter=Run  Up/Down=Select", RGB(60, 60, 90), RGB(25, 25, 35), FONT_SMALL);
    }
}

static void perun_execute(PERunState* st) {
    st->running = true;
    strcpy(st->status, "Initializing Win32 shim...");

    win32_shim_init();

    const uint8_t* pe_data = nullptr;
    uint32_t pe_size = 0;
    uint8_t* allocated_pe = nullptr;

    if (st->selected == 0) {
        // Built-in demo
        allocated_pe = build_demo_pe(&pe_size);
        pe_data = allocated_pe;
        strcpy(st->status, "Loading built-in demo PE...");
    } else {
        // Load from disk
        int idx = st->selected - 1;
        if (idx < (int)st->catalog.count) {
            uint32_t sectors = (st->catalog.entries[idx].size_bytes + 511) / 512;
            pe_size = st->catalog.entries[idx].size_bytes;
            allocated_pe = (uint8_t*)kmalloc(sectors * 512);
            if (allocated_pe) {
                for (uint32_t s = 0; s < sectors; s++) {
                    if (!ata_read_sector(st->catalog.entries[idx].start_sector + s,
                                          allocated_pe + s * 512)) {
                        strcpy(st->status, "Disk read error!");
                        kfree(allocated_pe);
                        st->running = false;
                        return;
                    }
                }
                pe_data = allocated_pe;
            }
        }
    }

    if (!pe_data) {
        strcpy(st->status, "Failed to load PE data");
        st->running = false;
        return;
    }

    strcpy(st->status, "Parsing PE headers...");
    PELoadResult result = pe_load(pe_data, pe_size);

    if (!result.success) {
        ksprintf(st->status, "PE Error: %s", result.error);
        kprintf("[PERUN] Load failed: %s\n", result.error);
        st->running = false;
        st->finished = true;
        st->exit_code = -1;
        if (allocated_pe) kfree(allocated_pe);
        return;
    }

    strcpy(st->status, "Executing...");
    st->exit_code = pe_execute(&result);

    // Cleanup
    pe_unload(&result);
    if (allocated_pe) kfree(allocated_pe);

    st->running = false;
    st->finished = true;
    kprintf("[PERUN] Finished, exit code=%d\n", st->exit_code);
}

static void perun_key(Window* win, uint8_t key) {
    PERunState* st = (PERunState*)win->userdata;
    if (!st) return;

    if (st->running) return; // Ignore input during execution

    if (st->finished) {
        if (key == KEY_ENTER || key == 'r' || key == 'R') {
            // Run again
            st->finished = false;
            perun_execute(st);
        } else if (key == KEY_BACKSPACE || key == 'b' || key == 'B' || key == KEY_ESCAPE) {
            st->finished = false;
        }
        return;
    }

    int max_idx = (int)st->catalog.count; // +1 for built-in, but catalog.count doesn't include it

    if (key == KEY_UP && st->selected > 0) st->selected--;
    if (key == KEY_DOWN && st->selected < max_idx) st->selected++;
    if (key == KEY_ENTER) {
        perun_execute(st);
    }
}

static void perun_mouse(Window* win, int mx, int my, bool left, bool right) {
    (void)right;
    if (!left) return;
    PERunState* st = (PERunState*)win->userdata;
    if (!st) return;

    int cx, cy, cw, ch;
    wm_get_content_area(win, &cx, &cy, &cw, &ch);
    (void)cw;

    if (st->finished) {
        // "Run Again" button
        if (my >= cy + ch - 22 && my < cy + ch - 4) {
            if (mx >= cx + 8 && mx < cx + 88) {
                st->finished = false;
                perun_execute(st);
            } else if (mx >= cx + 96 && mx < cx + 156) {
                st->finished = false;
            }
        }
        return;
    }

    if (st->running) return;

    // Click on program list
    int list_y = cy + 30 + 16; // After "Available Programs:" label
    int rel = my - list_y;
    if (rel >= 0) {
        int idx = rel / 16;
        int max_idx = (int)st->catalog.count;
        if (idx >= 0 && idx <= max_idx) st->selected = idx;
    }

    // Run button
    if (my >= cy + ch - 22 && my < cy + ch - 4 && mx >= cx + 8 && mx < cx + 68) {
        perun_execute(st);
    }
}

static void perun_close(Window* win) {
    if (win->userdata) kfree(win->userdata);
}

extern "C" void app_launch_perun() {
    Window* w = wm_create_window("PE32 Loader", 150, 80, 380, 340,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE | WIN_RESIZABLE);
    if (!w) return;

    PERunState* st = (PERunState*)kmalloc(sizeof(PERunState));
    memset(st, 0, sizeof(PERunState));

    // Try to load catalog from disk
    uint8_t sector_buf[512];
    if (ata_read_sector(PE_CATALOG_SECTOR, sector_buf)) {
        PECatalog* cat = (PECatalog*)sector_buf;
        if (cat->magic == PE_CATALOG_MAGIC && cat->count <= PE_MAX_ENTRIES) {
            memcpy(&st->catalog, cat, sizeof(PECatalog));
            st->catalog_loaded = true;
            kprintf("[PERUN] Catalog loaded: %d entries\n", st->catalog.count);
        }
    }

    if (!st->catalog_loaded) {
        st->demo_mode = true;
        kprintf("[PERUN] No catalog on disk, demo mode only\n");
    }

    w->userdata = st;
    w->on_draw = perun_draw;
    w->on_key = perun_key;
    w->on_mouse = perun_mouse;
    w->on_close = perun_close;
}
