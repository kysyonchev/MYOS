#include "screen.h"
#include "shell.h"
#include "fat12.h"
#include "interrupts.h"
#include "keyboard.h"
#include "memory.h"
#include "task.h"
#include "settings.h"

static void fpu_init(void) {
    unsigned int cr0, cr4;
    /* Clear EM (bit 2 = FPU emulation), set MP (bit 1 = monitor coprocessor),
     * clear TS (bit 3 = task switched). */
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 = (cr0 & ~(1u << 2)) | (1u << 1);
    cr0 &= ~(1u << 3);
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0));

    /* Enable FXSAVE/FXRSTOR and SIMD exception reporting in CR4. */
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1u << 9) | (1u << 10);
    __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4));

    /* Initialize the x87 FPU. */
    __asm__ volatile ("fninit");
}

void kernel_main(void) {
    terminal_initialize();
    fpu_init();

    idt_init();
    pic_init();
    keyboard_init();
    timer_init(100);
    memory_init();
    tasking_init();

    __asm__ volatile ("sti");

    if (fat12_init() < 0) {
        terminal_write_color("Warning: no filesystem detected.\n",
                             VGA_COLOR_LIGHT_RED);
    }

    settings_load();
    settings_apply();

    shell_run();
    while (1) { __asm__ volatile ("hlt"); }
}
