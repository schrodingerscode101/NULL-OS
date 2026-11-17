# NULLOS — Operating System

This archive contains the complete, ready-to-compile x86 32-bit Operating System built from scratch in **C++** and **x86 NASM Assembly**.

## Included Subsystems
1. **`boot.asm`**: Multiboot v1 compliant header, 32 KB kernel stack initialization, Global Descriptor Table (`lgdt`) flush routine, Interrupt Descriptor Table (`lidt`) loader, and full CPU register context saving ISR/IRQ stubs.
2. **`kernel.cpp` & `kernel.h`**:
   - **GDT & IDT**: Flat 4 GB Ring-0 Code/Data descriptors and remapped 8259 Programmable Interrupt Controller (IRQ0–IRQ15 mapped to INT 32–47).
   - **Hardware Drivers**: VGA Text Buffer (`0xB8000`) with hardware cursor and live top status bar, PIT 100 Hz system clock (`IRQ0`), and interrupt-driven PS/2 Keyboard driver (`IRQ1`).
   - **Memory Management**: Hybrid Free-List + Bump Kernel Heap Allocator (`kmalloc` / `kfree`) managing `0x00400000`–`0x00800000`.
   - **RamFS**: In-memory virtual filesystem supporting `ls`, `cat`, `write`, and `rm`.
   - **Interactive Shell**: Built-in commands (`help`, `sysinfo`, `memstat`, `alloc`, `ls`, `cat`, `write`, `rm`, `uptime`, `clear`, `reboot`).

## Quick Start (Ubuntu / Debian / WSL2)
```bash
sudo apt update
sudo apt install -y build-essential gcc-multilib g++-multilib nasm qemu-system-x86 grub-pc-bin xorriso

# Compile and boot immediately in QEMU:
make run
```
