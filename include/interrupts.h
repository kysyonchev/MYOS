#ifndef INTERRUPTS_H
#define INTERRUPTS_H

typedef struct {
    unsigned int ds;
    unsigned int edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    unsigned int int_no, err_code;
    unsigned int eip, cs, eflags;
} registers_t;

void idt_init(void);
void irq_install_handler(int irq, void (*handler)(void));
unsigned int timer_ticks(void);
void pic_init(void);
void pic_unmask(int irq);
void timer_init(unsigned int hz);

#endif
