#include "interrupts.h"
#include "screen.h"

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

struct idt_entry {
    u16 offset_low;
    u16 selector;
    u8  zero;
    u8  flags;
    u16 offset_high;
} __attribute__((packed));

struct idt_ptr {
    u16 limit;
    u32 base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr idtp;
static void (*irq_handlers[16])(void) = { 0 };

extern void isr0(void); extern void isr1(void); extern void isr2(void);
extern void isr3(void); extern void isr4(void); extern void isr5(void);
extern void isr6(void); extern void isr7(void); extern void isr8(void);
extern void isr9(void); extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);

extern void irq0(void);  extern void irq1(void);  extern void irq2(void);
extern void irq3(void);  extern void irq4(void);  extern void irq5(void);
extern void irq6(void);  extern void irq7(void);  extern void irq8(void);
extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void);
extern void irq15(void);

static inline unsigned char inb(unsigned short port) {
    unsigned char r;
    __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}
static inline void outb(unsigned short port, unsigned char v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}

static void idt_set_gate(int n, u32 handler) {
    idt[n].offset_low  = handler & 0xFFFF;
    idt[n].selector    = 0x08;
    idt[n].zero        = 0;
    idt[n].flags       = 0x8E;
    idt[n].offset_high = (handler >> 16) & 0xFFFF;
}

static const char* exception_names[32] = {
    "Divide by zero",
    "Debug",
    "Non-maskable interrupt",
    "Breakpoint",
    "Overflow",
    "Bound range exceeded",
    "Invalid opcode",
    "Device not available",
    "Double fault",
    "Coprocessor segment overrun",
    "Invalid TSS",
    "Segment not present",
    "Stack-segment fault",
    "General protection fault",
    "Page fault",
    "Reserved",
    "x87 floating-point",
    "Alignment check",
    "Machine check",
    "SIMD floating-point",
    "Virtualization",
    "Control protection",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved"
};

static void put_hex32(u32 v) {
    const char* hx = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--) {
        terminal_putchar(hx[(v >> (i * 4)) & 0xF]);
    }
}

static void put_dec(u32 v) {
    char tmp[12]; int n = 0;
    if (v == 0) { terminal_putchar('0'); return; }
    while (v > 0) { tmp[n++] = '0' + (v % 10); v /= 10; }
    while (n > 0) terminal_putchar(tmp[--n]);
}

volatile int g_in_gfx_mode = 0;

void irq_install_handler(int irq, void (*handler)(void)) {
    irq_handlers[irq] = handler;
}

extern volatile int g_in_gfx_mode;
extern void vga_set_mode3(void);

void isr_handler(registers_t* r) {
    if (r->int_no < 32) {
        if (g_in_gfx_mode) {
            vga_set_mode3();
            g_in_gfx_mode = 0;
            /* Wipe text memory clean */
            unsigned short* txt = (unsigned short*)0xB8000;
            for (int i = 0; i < 80 * 25; i++) txt[i] = 0x0720;
        }
        terminal_write("\n\n*** EXCEPTION ");
        put_dec(r->int_no);
        terminal_write(": ");
        terminal_write(exception_names[r->int_no]);
        terminal_write(" ***\n");
        terminal_write("EIP = 0x");
        put_hex32(r->eip);
        terminal_write("  ERR = 0x");
        put_hex32(r->err_code);
        terminal_write("\nSystem halted.\n");
        while (1) { __asm__ volatile ("cli; hlt"); }
    }

    if (r->int_no >= 32 && r->int_no < 48) {
        int irq = r->int_no - 32;
        if (irq_handlers[irq]) irq_handlers[irq]();
        if (irq >= 8) outb(0xA0, 0x20);
        outb(0x20, 0x20);
    }
}

void idt_init(void) {
    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (u32)&idt;

    u8* p = (u8*)idt;
    for (u32 i = 0; i < sizeof(idt); i++) p[i] = 0;

    idt_set_gate(0,  (u32)isr0);  idt_set_gate(1,  (u32)isr1);
    idt_set_gate(2,  (u32)isr2);  idt_set_gate(3,  (u32)isr3);
    idt_set_gate(4,  (u32)isr4);  idt_set_gate(5,  (u32)isr5);
    idt_set_gate(6,  (u32)isr6);  idt_set_gate(7,  (u32)isr7);
    idt_set_gate(8,  (u32)isr8);  idt_set_gate(9,  (u32)isr9);
    idt_set_gate(10, (u32)isr10); idt_set_gate(11, (u32)isr11);
    idt_set_gate(12, (u32)isr12); idt_set_gate(13, (u32)isr13);
    idt_set_gate(14, (u32)isr14); idt_set_gate(15, (u32)isr15);
    idt_set_gate(16, (u32)isr16); idt_set_gate(17, (u32)isr17);
    idt_set_gate(18, (u32)isr18); idt_set_gate(19, (u32)isr19);
    idt_set_gate(20, (u32)isr20); idt_set_gate(21, (u32)isr21);
    idt_set_gate(22, (u32)isr22); idt_set_gate(23, (u32)isr23);
    idt_set_gate(24, (u32)isr24); idt_set_gate(25, (u32)isr25);
    idt_set_gate(26, (u32)isr26); idt_set_gate(27, (u32)isr27);
    idt_set_gate(28, (u32)isr28); idt_set_gate(29, (u32)isr29);
    idt_set_gate(30, (u32)isr30); idt_set_gate(31, (u32)isr31);

    idt_set_gate(32, (u32)irq0);  idt_set_gate(33, (u32)irq1);
    idt_set_gate(34, (u32)irq2);  idt_set_gate(35, (u32)irq3);
    idt_set_gate(36, (u32)irq4);  idt_set_gate(37, (u32)irq5);
    idt_set_gate(38, (u32)irq6);  idt_set_gate(39, (u32)irq7);
    idt_set_gate(40, (u32)irq8);  idt_set_gate(41, (u32)irq9);
    idt_set_gate(42, (u32)irq10); idt_set_gate(43, (u32)irq11);
    idt_set_gate(44, (u32)irq12); idt_set_gate(45, (u32)irq13);
    idt_set_gate(46, (u32)irq14); idt_set_gate(47, (u32)irq15);

    __asm__ volatile ("lidt %0" : : "m"(idtp));
}
