CROSS = /c/msys64/opt/cross/bin/i686-elf-
CC = $(CROSS)gcc
CXX = $(CROSS)g++
AS = /c/msys64/usr/bin/nasm
LD = $(CROSS)ld

CXXFLAGS = -ffreestanding -O2 -Wall -Wextra -fno-exceptions -fno-rtti -fno-use-cxa-atexit -std=c++17
LDFLAGS = -T linker.ld -nostdlib
ASFLAGS = -f elf32

KERNEL = gateway2.elf

# Source files
ASM_SRCS = boot/boot.asm

CXX_SRCS = \
    kernel/kernel.cpp \
    kernel/gdt.cpp \
    kernel/idt.cpp \
    kernel/timer.cpp \
    kernel/panic.cpp \
    kernel/nvstore.cpp \
    drivers/serial.cpp \
    drivers/vga_text.cpp \
    drivers/framebuffer.cpp \
    drivers/keyboard.cpp \
    drivers/mouse.cpp \
    drivers/speaker.cpp \
    drivers/pci.cpp \
    drivers/ata.cpp \
    drivers/e1000.cpp \
    memory/pmm.cpp \
    memory/heap.cpp \
    lib/string.cpp \
    lib/printf.cpp \
    lib/math.cpp \
    gui/font.cpp \
    gui/theme.cpp \
    gui/window.cpp \
    gui/dock.cpp \
    gui/menu.cpp \
    gui/desktop.cpp \
    apps/terminal.cpp \
    apps/calculator.cpp \
    apps/clock.cpp \
    apps/edit.cpp \
    apps/sysmon.cpp \
    apps/games.cpp \
    apps/games2.cpp \
    apps/scifi.cpp \
    apps/tools.cpp \
    apps/intel.cpp \
    apps/extras.cpp \
    apps/gmail.cpp \
    apps/java.cpp \
    apps/perun.cpp \
    kernel/pe_loader.cpp \
    kernel/win32_shim.cpp \
    kernel/clipboard.cpp \
    crypto/base64.cpp \
    crypto/sha256.cpp \
    crypto/aes.cpp \
    crypto/rsa.cpp \
    crypto/tls.cpp \
    net/net.cpp

ASM_OBJS = $(ASM_SRCS:.asm=.o)
CXX_OBJS = $(CXX_SRCS:.cpp=.o)
OBJS = $(ASM_OBJS) $(CXX_OBJS)

all: $(KERNEL)

$(KERNEL): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

%.o: %.asm
	$(AS) $(ASFLAGS) -o $@ $<

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

QEMU = /c/msys64/mingw64/bin/qemu-system-i386
USERDATA = userdata.img
QEMU_IMG = /c/msys64/mingw64/bin/qemu-img

# Create userdata disk if it doesn't exist (1MB raw image, persists across boots)
$(USERDATA):
	$(QEMU_IMG) create -f raw $(USERDATA) 1M

run: $(KERNEL) $(USERDATA)
	$(QEMU) -kernel $(KERNEL) -m 128M -serial stdio -vga std -device e1000,netdev=net0 -netdev user,id=net0 -drive file=$(USERDATA),format=raw,if=ide,index=1

run-bochs: $(KERNEL) $(USERDATA)
	$(QEMU) -kernel $(KERNEL) -m 128M -serial stdio -device VGA,vgamem_mb=16 -drive file=$(USERDATA),format=raw,if=ide,index=1

iso: $(KERNEL)
	mkdir -p isodir/boot/grub
	cp $(KERNEL) isodir/boot/
	cp grub.cfg isodir/boot/grub/
	grub-mkrescue -o gateway2.iso isodir

clean:
	rm -f $(OBJS) $(KERNEL) gateway2.iso
	rm -rf isodir

.PHONY: all run run-bochs iso clean
