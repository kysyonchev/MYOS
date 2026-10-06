#include "program.h"

/* COUNTER.COM — a background task.
 * It doesn't print anything; it just idles.  The kernel's scheduler
 * bumps the task's CPU-tick counter on every timer tick, so you can
 * watch its progress with the TASKS command. */

__attribute__((section(".text.entry")))
void _start(syscalls_t* sys) {
    (void)sys;
    while (1) {
        __asm__ volatile ("hlt");
    }
}
