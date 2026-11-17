# =============================================================================
# Makefile for NULLOS (32-bit x86 Multiboot Kernel)
# =============================================================================
AS      = nasm
CC      = g++
LD      = ld
QEMU    = qemu-system-i386

ASFLAGS = -f elf32
CFLAGS  = -m32 -ffreestanding -O2 -Wall -Wextra -fno-exceptions -fno-rtti -nostdlib -fno-stack-protector -fno-pie
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib

TARGET  = NULLOS.bin
ISO     = NULLOS.iso

all: $(TARGET)

boot.o: boot.asm
	$(AS) $(ASFLAGS) boot.asm -o boot.o

kernel.o: kernel.cpp kernel.h
	$(CC) $(CFLAGS) -c kernel.cpp -o kernel.o

$(TARGET): boot.o kernel.o linker.ld
	$(LD) $(LDFLAGS) -o $(TARGET) boot.o kernel.o

# Instant boot in QEMU without building an ISO
run: $(TARGET)
	$(QEMU) -kernel $(TARGET)

# Build a full bootable GRUB ISO image (requires grub-pc-bin and xorriso)
iso: $(TARGET)
	mkdir -p isodir/boot/grub
	cp $(TARGET) isodir/boot/$(TARGET)
	cp grub.cfg isodir/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) isodir

run-iso: iso
	$(QEMU) -cdrom $(ISO)

clean:
	rm -rf *.o $(TARGET) $(ISO) isodir
