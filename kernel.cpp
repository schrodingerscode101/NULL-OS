#include "kernel.h"

// ============================================================================
// 1. String & Utility Functions (Freestanding C++ Replacements)
// ============================================================================
size_t strlen(const char* str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int strncmp(const char* s1, const char* s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) { s1++; s2++; n--; }
    if (n == 0) return 0;
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

void strcpy(char* dest, const char* src) {
    while ((*dest++ = *src++));
}

void* memset(void* bufptr, int value, size_t size) {
    unsigned char* buf = (unsigned char*) bufptr;
    for (size_t i = 0; i < size; i++) buf[i] = (unsigned char) value;
    return bufptr;
}

void itoa(uint32_t n, char* str, int base) {
    int i = 0;
    if (n == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return;
    }
    while (n != 0) {
        uint32_t rem = n % base;
        str[i++] = (rem > 9) ? (rem - 10) + 'A' : rem + '0';
        n = n / base;
    }
    str[i] = '\0';
    int start = 0, end = i - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++; end--;
    }
}

// ============================================================================
// 2. VGA Text Mode Driver (0xB8000) with Hardware Cursor & Scrolling
// ============================================================================
volatile uint16_t* const VGA_MEMORY = (uint16_t*)0xB8000;
const int VGA_WIDTH = 80;
const int VGA_HEIGHT = 25;

int term_col = 0;
int term_row = 1; // Row 0 is reserved for the top status bar
uint8_t term_color = 0x0F;

uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg) {
    return fg | bg << 4;
}

uint16_t vga_entry(unsigned char uc, uint8_t color) {
    return (uint16_t) uc | (uint16_t) color << 8;
}

void update_cursor(int x, int y) {
    uint16_t pos = y * VGA_WIDTH + x;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void draw_status_bar(uint32_t ticks, size_t heap_used) {
    uint8_t bar_color = vga_entry_color(VGA_BLACK, VGA_LIGHT_CYAN);
    for (int x = 0; x < VGA_WIDTH; x++) {
        VGA_MEMORY[x] = vga_entry(' ', bar_color);
    }
    const char* title = " AetherOS v1.0 [x86_32 Ring-0] | Up: ";
    int idx = 0;
    while (title[idx]) {
        VGA_MEMORY[idx] = vga_entry(title[idx], bar_color);
        idx++;
    }
    char buf[16];
    itoa(ticks / 100, buf, 10);
    for (int i = 0; buf[i]; i++) VGA_MEMORY[idx++] = vga_entry(buf[i], bar_color);
    VGA_MEMORY[idx++] = vga_entry('s', bar_color);

    const char* mem_lbl = " | Heap: ";
    for (int i = 0; mem_lbl[i]; i++) VGA_MEMORY[idx++] = vga_entry(mem_lbl[i], bar_color);
    itoa(heap_used, buf, 10);
    for (int i = 0; buf[i]; i++) VGA_MEMORY[idx++] = vga_entry(buf[i], bar_color);
    VGA_MEMORY[idx++] = vga_entry('B', bar_color);
}

void term_scroll() {
    if (term_row >= VGA_HEIGHT) {
        for (int y = 1; y < VGA_HEIGHT - 1; y++) {
            for (int x = 0; x < VGA_WIDTH; x++) {
                VGA_MEMORY[y * VGA_WIDTH + x] = VGA_MEMORY[(y + 1) * VGA_WIDTH + x];
            }
        }
        for (int x = 0; x < VGA_WIDTH; x++) {
            VGA_MEMORY[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', term_color);
        }
        term_row = VGA_HEIGHT - 1;
    }
}

void term_clear() {
    for (int y = 1; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            VGA_MEMORY[y * VGA_WIDTH + x] = vga_entry(' ', term_color);
        }
    }
    term_col = 0;
    term_row = 1;
    update_cursor(term_col, term_row);
}

void term_putc(char c) {
    if (c == '\n') {
        term_col = 0;
        term_row++;
        term_scroll();
        update_cursor(term_col, term_row);
        return;
    }
    if (c == '\b') {
        if (term_col > 0) {
            term_col--;
            VGA_MEMORY[term_row * VGA_WIDTH + term_col] = vga_entry(' ', term_color);
            update_cursor(term_col, term_row);
        }
        return;
    }
    VGA_MEMORY[term_row * VGA_WIDTH + term_col] = vga_entry(c, term_color);
    if (++term_col == VGA_WIDTH) {
        term_col = 0;
        term_row++;
        term_scroll();
    }
    update_cursor(term_col, term_row);
}

void term_print(const char* str) {
    for (size_t i = 0; str[i] != '\0'; i++) term_putc(str[i]);
}

void term_print_color(const char* str, enum vga_color fg) {
    uint8_t prev = term_color;
    term_color = vga_entry_color(fg, VGA_BLACK);
    term_print(str);
    term_color = prev;
}

void term_print_int(uint32_t num, int base) {
    char buf[32];
    itoa(num, buf, base);
    term_print(buf);
}

// ============================================================================
// 3. Global Descriptor Table (GDT) & Interrupt Descriptor Table (IDT)
// ============================================================================
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct gdt_entry gdt[3];
struct gdt_ptr gp;
extern "C" void gdt_flush(uint32_t);

void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[num].access = access;
}

void gdt_init() {
    gp.limit = (sizeof(struct gdt_entry) * 3) - 1;
    gp.base = (uint32_t)&gdt;
    gdt_set_gate(0, 0, 0, 0, 0);                // Null segment
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); // Kernel Code Segment (0x08)
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); // Kernel Data Segment (0x10)
    gdt_flush((uint32_t)&gp);
}

struct idt_entry {
    uint16_t base_lo;
    uint16_t sel;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t base_hi;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct idt_entry idt[256];
struct idt_ptr idtp;
extern "C" void idt_load(uint32_t);
extern "C" void irq0();
extern "C" void irq1();

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_lo = (base & 0xFFFF);
    idt[num].base_hi = (base >> 16) & 0xFFFF;
    idt[num].sel = sel;
    idt[num].always0 = 0;
    idt[num].flags = flags;
}

void pic_remap() {
    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();
    outb(0x21, 0x20); io_wait(); // Master PIC offset 32 (0x20)
    outb(0xA1, 0x28); io_wait(); // Slave PIC offset 40 (0x28)
    outb(0x21, 0x04); io_wait();
    outb(0xA1, 0x02); io_wait();
    outb(0x21, 0x01); io_wait();
    outb(0xA1, 0x01); io_wait();
    outb(0x21, 0xFC); // Unmask IRQ0 (Timer) and IRQ1 (Keyboard)
    outb(0xA1, 0xFF);
}

void idt_init() {
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base = (uint32_t)&idt;
    memset(&idt, 0, sizeof(struct idt_entry) * 256);
    pic_remap();
    idt_set_gate(32, (uint32_t)irq0, 0x08, 0x8E);
    idt_set_gate(33, (uint32_t)irq1, 0x08, 0x8E);
    idt_load((uint32_t)&idtp);
}

// ============================================================================
// 4. Kernel Heap Allocator (Free-List + Bump Hybrid)
// ============================================================================
const uint32_t HEAP_START = 0x00400000; // 4 MB physical address
const uint32_t HEAP_MAX   = 0x00800000; // 8 MB physical address (4 MB Heap)

struct mem_block {
    size_t size;
    bool free;
    mem_block* next;
};

uint32_t heap_top = HEAP_START;
size_t total_allocated_bytes = 0;
size_t allocation_count = 0;
mem_block* free_list_head = nullptr;

void* kmalloc(size_t size) {
    if (size == 0) return nullptr;
    size = (size + 7) & ~7; // 8-byte alignment

    // Search existing free list first
    mem_block* curr = free_list_head;
    while (curr) {
        if (curr->free && curr->size >= size) {
            curr->free = false;
            total_allocated_bytes += curr->size;
            allocation_count++;
            return (void*)((uint8_t*)curr + sizeof(mem_block));
        }
        curr = curr->next;
    }

    // Allocate new block from top of heap
    if (heap_top + sizeof(mem_block) + size > HEAP_MAX) return nullptr;
    mem_block* block = (mem_block*)heap_top;
    block->size = size;
    block->free = false;
    block->next = free_list_head;
    free_list_head = block;

    heap_top += sizeof(mem_block) + size;
    total_allocated_bytes += size;
    allocation_count++;
    return (void*)((uint8_t*)block + sizeof(mem_block));
}

void kfree(void* ptr) {
    if (!ptr) return;
    mem_block* block = (mem_block*)((uint8_t*)ptr - sizeof(mem_block));
    if (!block->free) {
        block->free = true;
        total_allocated_bytes -= block->size;
    }
}

// ============================================================================
// 5. In-Memory Virtual File System (RamFS)
// ============================================================================
struct ram_file {
    char name[32];
    char* content;
    size_t size;
    bool used;
};

const int MAX_FILES = 16;
ram_file fs_table[MAX_FILES];

void ramfs_init() {
    memset(fs_table, 0, sizeof(fs_table));

    // Seed default hackathon files
    strcpy(fs_table[0].name, "readme.txt");
    const char* msg0 = "Welcome to AetherOS! Built from scratch in C++ & x86 Assembly.\nUse 'help' to inspect kernel subsystems.";
    fs_table[0].size = strlen(msg0);
    fs_table[0].content = (char*)kmalloc(fs_table[0].size + 1);
    strcpy(fs_table[0].content, msg0);
    fs_table[0].used = true;

    strcpy(fs_table[1].name, "arch.sys");
    const char* msg1 = "BOOT=Multiboot_v1\nCPU=i686_Protected_Mode_Ring0\nMEM_BASE=0x00400000\nIRQ=8259_PIC_Remapped";
    fs_table[1].size = strlen(msg1);
    fs_table[1].content = (char*)kmalloc(fs_table[1].size + 1);
    strcpy(fs_table[1].content, msg1);
    fs_table[1].used = true;
}

// ============================================================================
// 6. Hardware Diagnostics (CPUID & PIT Timer)
// ============================================================================
volatile uint32_t timer_ticks = 0;

void pit_init(uint32_t frequency) {
    uint32_t divisor = 1193180 / frequency;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

void get_cpu_vendor(char* vendor_out) {
    uint32_t ebx, ecx, edx;
    asm volatile("cpuid"
                 : "=b"(ebx), "=d"(edx), "=c"(ecx)
                 : "a"(0));
    ((uint32_t*)vendor_out)[0] = ebx;
    ((uint32_t*)vendor_out)[1] = edx;
    ((uint32_t*)vendor_out)[2] = ecx;
    vendor_out[12] = '\0';
}

// ============================================================================
// 7. Interactive Kernel Shell & Command Parser
// ============================================================================
char cmd_buffer[128];
int cmd_len = 0;

void print_prompt() {
    term_print_color("root@aether", VGA_LIGHT_GREEN);
    term_print(":");
    term_print_color("/", VGA_LIGHT_CYAN);
    term_print("# ");
}

void execute_command(const char* cmd) {
    if (strlen(cmd) == 0) return;

    if (strcmp(cmd, "help") == 0) {
        term_print_color("AetherOS Kernel Shell Commands:\n", VGA_YELLOW);
        term_print("  help              - Show this command reference\n");
        term_print("  sysinfo           - Hardware CPUID, TSC cycles, and kernel memory layout\n");
        term_print("  memstat           - Inspect live Free-List Heap Allocator blocks\n");
        term_print("  alloc <bytes>     - Dynamically allocate <bytes> on kernel heap\n");
        term_print("  ls                - List files in RamFS in-memory filesystem\n");
        term_print("  cat <file>        - Display contents of a file in RamFS\n");
        term_print("  write <f> <text>  - Create or overwrite a file in RamFS\n");
        term_print("  rm <file>         - Delete a file from RamFS and free heap memory\n");
        term_print("  uptime            - Display PIT IRQ0 hardware timer ticks & seconds\n");
        term_print("  clear             - Clear terminal viewport\n");
        term_print("  reboot            - Pulse 8042 PS/2 controller reset line\n");
    }
    else if (strcmp(cmd, "clear") == 0) {
        term_clear();
    }
    else if (strcmp(cmd, "sysinfo") == 0) {
        char vendor[13];
        get_cpu_vendor(vendor);
        uint64_t tsc = rdtsc();
        term_print_color("=== Hardware & Kernel Telemetry ===\n", VGA_LIGHT_CYAN);
        term_print("  CPU Vendor ID   : "); term_print(vendor); term_print("\n");
        term_print("  CPU Mode        : 32-bit x86 Protected Mode (Ring 0)\n");
        term_print("  TSC Cycle Count : 0x"); term_print_int((uint32_t)(tsc >> 32), 16);
        term_print_int((uint32_t)(tsc & 0xFFFFFFFF), 16); term_print("\n");
        term_print("  VGA Text Buffer : 0x000B8000 (80x25 Color Matrix)\n");
        term_print("  Heap Region     : 0x00400000 - 0x00800000 (4096 KB)\n");
    }
    else if (strcmp(cmd, "memstat") == 0) {
        term_print_color("=== Kernel Heap Allocator Status ===\n", VGA_LIGHT_CYAN);
        term_print("  Heap Base       : 0x"); term_print_int(HEAP_START, 16); term_print("\n");
        term_print("  Heap Top Break  : 0x"); term_print_int(heap_top, 16); term_print("\n");
        term_print("  Active Payload  : "); term_print_int(total_allocated_bytes, 10); term_print(" bytes\n");
        term_print("  Total Allocs    : "); term_print_int(allocation_count, 10); term_print("\n");
        term_print("  Block Chain     :\n");
        mem_block* curr = free_list_head;
        int idx = 0;
        while (curr && idx < 10) {
            term_print("    [Block "); term_print_int(idx++, 10);
            term_print("] Addr: 0x"); term_print_int((uint32_t)curr, 16);
            term_print(" | Size: "); term_print_int(curr->size, 10);
            term_print("B | Status: ");
            if (curr->free) term_print_color("FREE\n", VGA_LIGHT_GREEN);
            else term_print_color("USED\n", VGA_YELLOW);
            curr = curr->next;
        }
    }
    else if (strncmp(cmd, "alloc ", 6) == 0) {
        const char* num_str = cmd + 6;
        uint32_t req = 0;
        while (*num_str >= '0' && *num_str <= '9') {
            req = req * 10 + (*num_str - '0');
            num_str++;
        }
        if (req == 0) {
            term_print_color("Usage: alloc <positive_byte_size>\n", VGA_LIGHT_RED);
        } else {
            void* ptr = kmalloc(req);
            if (ptr) {
                term_print_color("Allocated ", VGA_LIGHT_GREEN);
                term_print_int(req, 10);
                term_print(" bytes at physical address 0x");
                term_print_int((uint32_t)ptr, 16);
                term_print("\n");
            } else {
                term_print_color("Out of kernel heap memory!\n", VGA_LIGHT_RED);
            }
        }
    }
    else if (strcmp(cmd, "ls") == 0) {
        term_print_color("RamFS Directory Listing (/):\n", VGA_YELLOW);
        int count = 0;
        for (int i = 0; i < MAX_FILES; i++) {
            if (fs_table[i].used) {
                term_print("  ");
                term_print_color(fs_table[i].name, VGA_LIGHT_CYAN);
                term_print("  (");
                term_print_int(fs_table[i].size, 10);
                term_print(" bytes)\n");
                count++;
            }
        }
        if (count == 0) term_print("  (empty filesystem)\n");
    }
    else if (strncmp(cmd, "cat ", 4) == 0) {
        const char* fname = cmd + 4;
        for (int i = 0; i < MAX_FILES; i++) {
            if (fs_table[i].used && strcmp(fs_table[i].name, fname) == 0) {
                term_print(fs_table[i].content);
                term_print("\n");
                return;
            }
        }
        term_print_color("File not found: ", VGA_LIGHT_RED);
        term_print(fname);
        term_print("\n");
    }
    else if (strncmp(cmd, "write ", 6) == 0) {
        const char* rest = cmd + 6;
        char fname[32];
        int i = 0;
        while (*rest && *rest != ' ' && i < 31) fname[i++] = *rest++;
        fname[i] = '\0';
        if (*rest == ' ') rest++;
        if (strlen(fname) == 0 || strlen(rest) == 0) {
            term_print_color("Usage: write <filename> <text_content>\n", VGA_LIGHT_RED);
            return;
        }
        // Check overwrite or free slot
        int slot = -1;
        for (int j = 0; j < MAX_FILES; j++) {
            if (fs_table[j].used && strcmp(fs_table[j].name, fname) == 0) {
                kfree(fs_table[j].content);
                slot = j;
                break;
            }
            if (!fs_table[j].used && slot == -1) slot = j;
        }
        if (slot == -1) {
            term_print_color("RamFS file table full!\n", VGA_LIGHT_RED);
            return;
        }
        strcpy(fs_table[slot].name, fname);
        fs_table[slot].size = strlen(rest);
        fs_table[slot].content = (char*)kmalloc(fs_table[slot].size + 1);
        strcpy(fs_table[slot].content, rest);
        fs_table[slot].used = true;
        term_print_color("Saved file: ", VGA_LIGHT_GREEN);
        term_print(fname);
        term_print("\n");
    }
    else if (strncmp(cmd, "rm ", 3) == 0) {
        const char* fname = cmd + 3;
        for (int i = 0; i < MAX_FILES; i++) {
            if (fs_table[i].used && strcmp(fs_table[i].name, fname) == 0) {
                kfree(fs_table[i].content);
                fs_table[i].used = false;
                term_print_color("Removed file and freed heap block: ", VGA_LIGHT_GREEN);
                term_print(fname);
                term_print("\n");
                return;
            }
        }
        term_print_color("File not found: ", VGA_LIGHT_RED);
        term_print(fname);
        term_print("\n");
    }
    else if (strcmp(cmd, "uptime") == 0) {
        term_print("PIT Timer Ticks (100 Hz): ");
        term_print_int(timer_ticks, 10);
        term_print(" (");
        term_print_int(timer_ticks / 100, 10);
        term_print(" seconds)\n");
    }
    else if (strcmp(cmd, "reboot") == 0) {
        term_print_color("Rebooting via 8042 PS/2 reset line...\n", VGA_YELLOW);
        uint8_t good = 0x02;
        while (good & 0x02) good = inb(0x64);
        outb(0x64, 0xFE);
        asm volatile("hlt");
    }
    else {
        term_print_color("Unknown command: '", VGA_LIGHT_RED);
        term_print(cmd);
        term_print_color("'. Type 'help' for available commands.\n", VGA_LIGHT_RED);
    }
}

// ============================================================================
// 8. PS/2 Keyboard Scancode Set 1 Driver & IRQ Dispatcher
// ============================================================================
const char kbd_us[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
  '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',   0,
 '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',   0, '*',
    0,  ' '
};

extern "C" void irq_handler(struct regs* r) {
    if (r->int_no == 32) { // IRQ0: PIT Timer
        timer_ticks++;
        if (timer_ticks % 20 == 0) {
            draw_status_bar(timer_ticks, total_allocated_bytes);
        }
    }
    else if (r->int_no == 33) { // IRQ1: PS/2 Keyboard
        uint8_t scancode = inb(0x60);
        if (!(scancode & 0x80)) { // Key press (bit 7 clear)
            char c = kbd_us[scancode];
            if (c == '\n') {
                term_putc('\n');
                cmd_buffer[cmd_len] = '\0';
                execute_command(cmd_buffer);
                cmd_len = 0;
                draw_status_bar(timer_ticks, total_allocated_bytes);
                print_prompt();
            }
            else if (c == '\b') {
                if (cmd_len > 0) {
                    cmd_len--;
                    term_putc('\b');
                }
            }
            else if (c >= ' ' && c <= '~' && cmd_len < 126) {
                cmd_buffer[cmd_len++] = c;
                term_putc(c);
            }
        }
    }

    // Send End-of-Interrupt (EOI) to PICs
    if (r->int_no >= 40) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

// ============================================================================
// 9. Kernel Entry Point
// ============================================================================
extern "C" void kernel_main(uint32_t magic, uint32_t multiboot_info) {
    (void)multiboot_info;
    gdt_init();
    idt_init();
    pit_init(100); // 100 Hz system timer
    ramfs_init();

    term_clear();
    draw_status_bar(0, total_allocated_bytes);

    term_print_color("   ___       __  __             ____  _____\n", VGA_LIGHT_CYAN);
    term_print_color("  / _ | ___ / /_/ /  ___ ____  / __ \\/ ___/\n", VGA_LIGHT_CYAN);
    term_print_color(" / __ |/ -_) __/ _ \\/ -_) __/ / /_/ (__  ) \n", VGA_LIGHT_CYAN);
    term_print_color("/_/ |_|\\__/\\__/_//_/\\__/_/    \\____/____/  \n", VGA_LIGHT_CYAN);
    term_print_color("32-Bit Protected Mode Hackathon Kernel (GDT + IDT + Heap + RamFS)\n\n", VGA_YELLOW);

    if (magic == 0x2BADB002) {
        term_print_color("[OK] ", VGA_LIGHT_GREEN);
        term_print("Verified Multiboot v1 bootloader signature (0x2BADB002)\n");
    }
    term_print_color("[OK] ", VGA_LIGHT_GREEN);
    term_print("Initialized Global & Interrupt Descriptor Tables (8259 PIC Remapped)\n");
    term_print_color("[OK] ", VGA_LIGHT_GREEN);
    term_print("Mounted Kernel Free-List Heap (0x400000) & RamFS Virtual Filesystem\n\n");
    term_print("Type 'help' to explore built-in kernel commands.\n\n");

    print_prompt();
    asm volatile("sti"); // Enable hardware interrupts

    while (1) {
        asm volatile("hlt"); // Put CPU in low-power halt state until next interrupt
    }
}
