#include "interrupts.h"
#include "task.h"

static volatile unsigned int g_ticks = 0;

static inline void outb(unsigned short port, unsigned char v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}

extern void task_reap(void);

unsigned int timer_c_handler(unsigned int old_esp) {
    g_ticks++;

    task_t* prev = task_current();
    if (prev) prev->counter++;

    task_reap();

    task_t* next = task_pick_next();
    if (!next || next == prev) return old_esp;

    prev->esp = old_esp;
    if (prev->state == TASK_RUNNING) prev->state = TASK_READY;
    next->state = TASK_RUNNING;
    task_set_current(next);

    return next->esp;
}

void timer_init(unsigned int hz) {
    unsigned int divisor = 1193182 / hz;
    if (divisor > 65535) divisor = 65535;
    if (divisor < 1) divisor = 1;

    outb(0x43, 0x36);
    outb(0x40, (unsigned char)(divisor & 0xFF));
    outb(0x40, (unsigned char)((divisor >> 8) & 0xFF));

    pic_unmask(0);
}

unsigned int timer_ticks(void) {
    return g_ticks;
}
