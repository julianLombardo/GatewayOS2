# GatewayOS2

A bare-metal x86 operating system with a NeXTSTEP-inspired graphical desktop environment, built entirely from scratch with no external libraries or runtime dependencies.

![Architecture: x86 32-bit](https://img.shields.io/badge/arch-x86__32-blue)
![Language: C/C++/ASM](https://img.shields.io/badge/lang-C%2FC%2B%2B%2FASM-orange)
![Apps: 50+](https://img.shields.io/badge/apps-50%2B-green)

## Features

- **VESA framebuffer** — 1024x768 32bpp graphics via Bochs VBE
- **Compositing window manager** — draggable windows, focus tracking, minimize, close
- **Desktop environment** — gradient wallpaper, horizontal menubar, right-click context menu, pixel-art dock
- **PS/2 keyboard & mouse** — full input handling with scan code translation
- **E1000 NIC + DHCP** — network stack with ARP, IP, UDP, TCP, DNS
- **ATA PIO storage** — persistent user data across reboots
- **PE32 loader** — runs Windows .exe files via Win32 shim layer (60+ API functions)
- **Clipboard** — system-wide copy/paste

## Applications (50+)

| Category | Apps |
|---|---|
| **Productivity** | Text Editor, Calculator, Calendar, Contacts, Notes, Mail Client |
| **Creative** | Paint, Draw |
| **Games** | Snake, Pong, Tetris, Minesweeper, Chess, 15-Puzzle, Billiards |
| **Sci-Fi** | GW-Decrypt, Radar, Neural Net, Matrix Rain, Uplink, StarMap, Comm, Probe |
| **Tools** | Terminal (15+ commands), System Monitor, Hex Viewer, Network Info, Screenshot, Preferences, About |
| **Intel** | GW-Cipher (6 ciphers + SHA-256), GW-Fortress (password analyzer), GW-Sentinel (system audit), GW-NetScan (port scanner), GW-Hashlab (4 hash algorithms) |
| **Dev** | Java IDE (editor + interpreter + console), PE32 Loader |
| **Mail** | Gmail client (sends real email via host relay) |

## Building

### Prerequisites (Windows / MSYS2)

1. Install [MSYS2](https://www.msys2.org/)
2. Build or install an `i686-elf` cross-compiler toolchain:
   - `i686-elf-gcc` / `i686-elf-g++` (GCC, targeting 32-bit freestanding ELF)
   - `i686-elf-ld` (GNU linker)
3. Install NASM: `pacman -S nasm`
4. Install QEMU: `pacman -S mingw-w64-x86_64-qemu`

The Makefile expects the cross-compiler at `/c/msys64/opt/cross/bin/i686-elf-*`. Edit the `CC`, `CXX`, `LD` variables in `Makefile` if your toolchain is elsewhere.

### Compile

```bash
make
```

This produces `gateway2.elf` (~100KB), a multiboot-compliant kernel.

### Run in QEMU

```bash
# Windows (double-click or run from cmd)
run.bat

# Or manually:
qemu-system-i386 -kernel gateway2.elf -m 128M -serial stdio -vga std \
  -device e1000,netdev=net0 -netdev user,id=net0 \
  -drive file=userdata.img,format=raw,if=ide,index=1
```

`run.bat` automatically:
- Creates `userdata.img` (1MB) for persistent storage if it doesn't exist
- Starts the mail relay (optional, for sending email)
- Launches QEMU

### Boot on real hardware (via GRUB)

```bash
make iso
```

Burns a bootable ISO to `gateway2.iso`. Write to USB with `dd` or Rufus.

## Usage

### Login Screen

On boot you'll see a login screen with three fields: **Username**, **Email**, and **Password**. Use `TAB` to switch fields and `ENTER` to log in. Credentials are saved to disk and auto-filled on next boot.

- Username is required (any value)
- Email and password are optional (used by the mail client)

### Desktop

- **Menubar** (top) — click category names to open app menus: System, Productivity, Creative, Games, Sci-Fi, Tools
- **Dock** (right edge) — quick-launch icons for frequently used apps
- **Right-click** — context menu on the desktop background
- **Windows** — drag title bars to move, click close/minimize buttons

### Terminal Commands

Open Terminal from Tools menu. Available commands:

```
help        Show all commands
echo        Print text
clear       Clear screen
mem         Show memory info
ps          List windows
time        Show uptime
calc        Inline calculator
cowsay      ASCII cow
color       Change text color
reboot      Restart the system
```

### Java IDE

Open from Productivity menu. Includes:
- Syntax-highlighted code editor
- Recursive-descent interpreter supporting `int`, `String`, `boolean`, arrays, `if/else`, `while`, `for`
- 8 sample programs (click `Samples` button)
- Click `Run` or press `F5` to execute

### Mail Client

Open Gateway Mail from Productivity menu. Enter your Gmail address and a [Google App Password](https://myaccount.google.com/apppasswords) (16-character code). The OS sends mail through `mail_relay.py` running on the host machine.

### PE32 Loader

Open from Tools menu. Runs Windows PE32 executables with a built-in Win32 API shim layer (60+ shimmed functions from kernel32, user32, msvcrt). Includes a demo program that calls `MessageBoxA`.

## Project Structure

```
GatewayOS2/
  boot/          x86 boot code (NASM)
  kernel/        Kernel core, GDT, IDT, timer, PE loader, Win32 shim
  drivers/       Hardware drivers (framebuffer, keyboard, mouse, E1000, ATA, PCI)
  gui/           Window manager, desktop, dock, menubar, font, theme
  apps/          All applications (~12 source files, 50+ apps)
  lib/           String, printf, math utilities
  memory/        Physical memory manager, heap allocator
  net/           Network stack (Ethernet, ARP, IP, UDP, TCP, DHCP, DNS)
  crypto/        AES-128, SHA-256, RSA, Base64, TLS structures
  mail_relay.py  Host-side email relay (Python)
  run.bat        Build + launch script
  Makefile        Build system
  linker.ld      Linker script
  grub.cfg       GRUB bootloader config
```

## Architecture

- **x86 32-bit protected mode**, flat memory model (no paging)
- **Single-tasking** cooperative model — one app draws at a time via window manager
- **Multiboot** compliant — boots via GRUB or direct QEMU `-kernel`
- **Freestanding C++** — no libc, no libstdc++, all runtime code is custom
- **~18,000 lines** of hand-written C/C++/ASM

## License

This project is provided as-is for educational and portfolio purposes.
