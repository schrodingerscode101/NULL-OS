; =============================================================================
; boot.asm - Multiboot Entry Point, GDT Loader, and Interrupt Stubs
; =============================================================================
MBALIGN  equ  1 << 0            ; Align loaded modules on page boundaries
MEMINFO  equ  1 << 1            ; Provide memory map from GRUB
FLAGS    equ  MBALIGN | MEMINFO ; Multiboot flag field
MAGIC    equ  0x1BADB002        ; Multiboot v1 magic number
CHECKSUM equ -(MAGIC + FLAGS)   ; Checksum proving multiboot compliance

section .multiboot
align 4
    dd MAGIC
    dd FLAGS
    dd CHECKSUM

section .bss
align 16
stack_bottom:
    resb 32768 ; 32 KB Kernel Stack
stack_top:

section .text
global _start
extern kernel_main

_start:
    mov esp, stack_top          ; Set stack pointer
    push ebx                    ; Pass Multiboot info struct pointer
    push eax                    ; Pass Multiboot magic number
    call kernel_main            ; Enter C++ Kernel
    cli
.hang:
    hlt
    jmp .hang

; -----------------------------------------------------------------------------
; GDT & IDT Assembly Helpers
; -----------------------------------------------------------------------------
global gdt_flush
gdt_flush:
    mov eax, [esp+4]            ; Get pointer to GDT descriptor
    lgdt [eax]                  ; Load GDT
    mov ax, 0x10                ; 0x10 is our kernel data segment offset
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.flush             ; Far jump to 0x08 (kernel code segment)
.flush:
    ret

global idt_load
idt_load:
    mov eax, [esp+4]            ; Get pointer to IDT descriptor
    lidt [eax]                  ; Load IDT
    ret

; -----------------------------------------------------------------------------
; Interrupt Service Routine (ISR) & IRQ Stubs
; -----------------------------------------------------------------------------
extern irq_handler

%macro IRQ 2
global irq%1
irq%1:
    cli
    push byte 0
    push byte %2
    jmp irq_common_stub
%endmacro

IRQ 0, 32   ; Programmable Interval Timer (PIT) - IRQ0 -> INT 32
IRQ 1, 33   ; PS/2 Keyboard                     - IRQ1 -> INT 33

irq_common_stub:
    pusha                       ; Push EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI
    mov ax, ds
    push eax                    ; Save data segment descriptor
    mov ax, 0x10                ; Load kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp                    ; Pass pointer to registers struct
    call irq_handler
    add esp, 4
    pop ebx                     ; Restore original data segment
    mov ds, bx
    mov es, bx
    mov fs, bx
    mov gs, bx
    popa                        ; Restore general registers
    add esp, 8                  ; Clean up pushed error code and IRQ number
    iret                        ; Return from interrupt
