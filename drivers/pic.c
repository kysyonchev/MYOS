#include "interrupts.h"

static inline unsigned char inb(unsigned short port) {
    unsigned char r;
    __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}
static inline void outb(unsigned short port, unsigned char v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

#define ICW1_INIT 0x10
#define ICW1_ICW4 0x01
#define ICW4_8086 0x01

void pic_init(void) {
    outb(PIC1_CMD,  ICW1_INIT | ICW1_ICW4);
    outb(PIC2_CMD,  ICW1_INIT | ICW1_ICW4);
    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28);
    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);
    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

void pic_unmask(int irq) {
    unsigned short port;
    unsigned char bit;
    if (irq < 8) { port = PIC1_DATA; bit = (unsigned char)irq; }
    else         { port = PIC2_DATA; bit = (unsigned char)(irq - 8); }

    unsigned char mask = inb(port);
    mask &= ~(1 << bit);
    outb(port, mask);

    if (irq >= 8) {
        outb(PIC1_DATA, inb(PIC1_DATA) & ~(1 << 2));
    }
}
