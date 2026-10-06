#include "keyboard.h"
#include "interrupts.h"

#define KBD_BUF_SIZE 256
#define KBD_BUF_MASK (KBD_BUF_SIZE - 1)

static volatile unsigned int kbd_head = 0;
static volatile unsigned int kbd_tail = 0;
static volatile char kbd_buf[KBD_BUF_SIZE];

static int shift_held = 0;
static int ctrl_held  = 0;
static int caps_lock  = 0;

/* 1 if the corresponding key is currently held down. */
static volatile unsigned char key_held[256];

static const char scancode_ascii[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t','q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,   '\\','z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0,   ' '
};

static const char scancode_ascii_shift[128] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t','Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0,   ' '
};

static inline unsigned char inb(unsigned short port) {
    unsigned char r;
    __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

static void kbd_push(char c) {
    unsigned int next = (kbd_head + 1) & KBD_BUF_MASK;
    if (next == kbd_tail) return;
    kbd_buf[kbd_head] = c;
    kbd_head = next;
}

static void clear_held_for_scancode(unsigned char sc) {
    if (sc >= 128) return;
    char c1 = scancode_ascii[sc];
    char c2 = scancode_ascii_shift[sc];
    if (c1) key_held[(unsigned char)c1] = 0;
    if (c2) key_held[(unsigned char)c2] = 0;
}

static void kbd_irq(void) {
    unsigned char sc = inb(0x60);

    /* Key release (bit 7 set) */
    if (sc & 0x80) {
        unsigned char rel = sc & 0x7F;

        if (rel == 0x2A || rel == 0x36) shift_held = 0;
        if (rel == 0x1D) ctrl_held = 0;

        /* Regular keys: clear both shifted and unshifted variants. */
        clear_held_for_scancode(rel);

        /* Special keys */
        if (rel == 0x48) { key_held[0x80] = 0; key_held[0x88] = 0; }
        if (rel == 0x50) { key_held[0x81] = 0; key_held[0x89] = 0; }
        if (rel == 0x4B) { key_held[0x82] = 0; }
        if (rel == 0x4D) { key_held[0x83] = 0; }
        if (rel == 0x53) { key_held[0x84] = 0; }
        if (rel == 0x47) { key_held[0x85] = 0; }
        if (rel == 0x4F) { key_held[0x86] = 0; }
        return;
    }

    if (sc == 0x2A || sc == 0x36) { shift_held = 1; return; }
    if (sc == 0x1D) { ctrl_held = 1; return; }
    if (sc == 0x3A) { caps_lock = !caps_lock; return; }

    if (sc >= 128) return;

    if (sc == 0x48) { char k = ctrl_held ? (char)0x88 : (char)0x80;
                      kbd_push(k); key_held[(unsigned char)k] = 1; return; }
    if (sc == 0x50) { char k = ctrl_held ? (char)0x89 : (char)0x81;
                      kbd_push(k); key_held[(unsigned char)k] = 1; return; }
    if (sc == 0x4B) { kbd_push((char)0x82); key_held[0x82] = 1; return; }
    if (sc == 0x4D) { kbd_push((char)0x83); key_held[0x83] = 1; return; }
    if (sc == 0x53) { kbd_push((char)0x84); key_held[0x84] = 1; return; }
    if (sc == 0x47) { kbd_push((char)0x85); key_held[0x85] = 1; return; }
    if (sc == 0x4F) { kbd_push((char)0x86); key_held[0x86] = 1; return; }

    char c;
    if (shift_held) c = scancode_ascii_shift[sc];
    else            c = scancode_ascii[sc];

    if (caps_lock && !shift_held && c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    if (caps_lock && shift_held && c >= 'A' && c <= 'Z') c = c - 'A' + 'a';

    if (c != 0) {
        kbd_push(c);
        key_held[(unsigned char)c] = 1;
    }
}

void keyboard_init(void) {
    while (inb(0x64) & 0x01) inb(0x60);

    kbd_head = 0;
    kbd_tail = 0;
    shift_held = 0;
    ctrl_held  = 0;
    caps_lock  = 0;

    for (int i = 0; i < 256; i++) key_held[i] = 0;

    irq_install_handler(1, kbd_irq);
    pic_unmask(1);
}

int keyboard_haschar(void) {
    return kbd_head != kbd_tail;
}

char keyboard_getchar(void) {
    while (kbd_head == kbd_tail) {
        __asm__ volatile ("hlt");
    }
    char c = kbd_buf[kbd_tail];
    kbd_tail = (kbd_tail + 1) & KBD_BUF_MASK;
    return c;
}

int keyboard_key_down(char c) {
    return key_held[(unsigned char)c] ? 1 : 0;
}
