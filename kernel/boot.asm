bits 32

section .multiboot
align 4
    dd 0x1BADB002              ; Multiboot magic
    dd 0x00000000              ; flags
    dd -(0x1BADB002 + 0x00000000) ; checksum

section .text

global start

extern kernel_main

start:
    cli
    mov esp, stack_top
    call kernel_main

hang:
    cli
    hlt
    jmp hang

section .bss
align 16
stack_bottom:
    resb 65536
stack_top: