#include "program.h"
#include "fat12.h"
#include "screen.h"
#include "keyboard.h"
#include "memory.h"
#include "interrupts.h"
#include "vga.h"

#define MAX_ARGS     16
#define ARG_BUF_SIZE 512

static void sys_print(const char* s) { terminal_write(s); }
static void sys_putchar(char c)      { terminal_putchar(c); }
static void sys_clear(void)          { terminal_clear(); }
static void sys_set_color(unsigned char c) { terminal_set_color(c); }
static char sys_getchar(void)        { return keyboard_getchar(); }
static void sys_exit(void)           { }

static void* sys_malloc(unsigned int size) { return kmalloc(size); }
static void  sys_free(void* p)             { kfree(p); }

static int sys_file_load(const char* name, char* buf, unsigned int max) {
    return fat12_cwd_read(name, (u8*)buf, max);
}
static int sys_file_save(const char* name, const char* buf, unsigned int size) {
    if (fat12_cwd_exists(name)) fat12_cwd_delete(name);
    return fat12_cwd_write(name, (const u8*)buf, size);
}
static int sys_file_exists(const char* name) { return fat12_cwd_exists(name); }
static unsigned int sys_file_size(const char* name) { return fat12_cwd_size(name); }

static void sys_gotoxy(int x, int y) { terminal_set_cursor(x, y); }
static unsigned int sys_ticks(void)   { return timer_ticks(); }

static unsigned int g_rand_state = 0;
static unsigned int sys_rand(void) {
    if (g_rand_state == 0) g_rand_state = timer_ticks() ^ 0x5EED1234u;
    g_rand_state = g_rand_state * 1103515245u + 12345u;
    return (g_rand_state >> 16) & 0x7FFF;
}
static int sys_kbhit(void) { return keyboard_haschar(); }

static void sys_sleep_ms(unsigned int ms) {
    unsigned int target = timer_ticks() + (ms / 10);
    while (timer_ticks() < target) { __asm__ volatile ("hlt"); }
}
static void sys_get_cursor(int* x, int* y) {
    int cx = 0, cy = 0;
    terminal_get_cursor(&cx, &cy);
    if (x) *x = cx;
    if (y) *y = cy;
}

static inline unsigned char rtc_inb(unsigned short port) {
    unsigned char r;
    __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}
static inline void rtc_outb(unsigned short port, unsigned char v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}
static unsigned char rtc_bcd_to_bin(unsigned char v) {
    return (unsigned char)((v & 0x0F) + ((v >> 4) * 10));
}
static void sys_get_rtc(int* h, int* m, int* s) {
    rtc_outb(0x70, 0x00); unsigned char sec = rtc_inb(0x71);
    rtc_outb(0x70, 0x02); unsigned char min = rtc_inb(0x71);
    rtc_outb(0x70, 0x04); unsigned char hr  = rtc_inb(0x71);
    if (h) *h = rtc_bcd_to_bin(hr);
    if (m) *m = rtc_bcd_to_bin(min);
    if (s) *s = rtc_bcd_to_bin(sec);
}

/* ---------- v6 graphics ---------- */

static void sys_gfx_init(void) { vga_set_mode13(); }

extern volatile int g_in_gfx_mode;
static void sys_gfx_exit(void) {
    g_in_gfx_mode = 0;
    /* Mode-13h -> mode-3 restore is unreliable in QEMU.  Instead, do what
     * DOS games did: end the session and reset the machine.  The user is
     * back in text mode in ~1 second. */
    vga_clear(0);

    /* Paint a short "session over" message in the framebuffer using a tiny
     * pixel script, so the user knows what's happening. */
    /* (Skipped for simplicity — just black screen then reset.) */

    for (volatile int i = 0; i < 10000000; i++) { }   /* pause */
    for (volatile int i = 0; i < 10000000; i++) { }

    /* Pulse the reset line */
    __asm__ volatile ("outb %0, %1" : : "a"((unsigned char)0xFE), "Nd"((unsigned short)0x64));
    __asm__ volatile ("outb %0, %1" : : "a"((unsigned char)0x02), "Nd"((unsigned short)0xCF9));
    __asm__ volatile ("outb %0, %1" : : "a"((unsigned char)0x06), "Nd"((unsigned short)0xCF9));

    while (1) { __asm__ volatile ("cli; hlt"); }
}

static void sys_gfx_clear(unsigned char c) { vga_clear(c); }
static void sys_gfx_pixel(int x, int y, unsigned char c) { vga_pixel(x, y, c); }
static void sys_gfx_vsync(void) { vga_vsync(); }
static void sys_gfx_blit(const unsigned char* buf) { vga_blit(buf); }

static int sys_key_down(char c) { return keyboard_key_down(c); }

static void sys_putchar_at(int x, int y, char c, unsigned char color) {
    if (x < 0 || x >= 80 || y < 0 || y >= 25) return;
    unsigned short* vga = (unsigned short*)0xB8000;
    vga[y * 80 + x] = (unsigned short)(unsigned char)c |
                      ((unsigned short)color << 8);
}

static void sys_gfx_color(unsigned char i, unsigned char r, unsigned char g, unsigned char b) {
    vga_set_palette(i, r, g, b);
}

static const syscalls_t g_template = {
    sys_print, sys_putchar, sys_clear, sys_set_color, sys_getchar, sys_exit,
    0, 0,
    sys_malloc, sys_free, sys_file_load, sys_file_save, sys_file_exists, sys_file_size,
    sys_gotoxy, sys_ticks, sys_rand, sys_kbhit,
    sys_sleep_ms, sys_get_cursor, sys_get_rtc,
    sys_gfx_init, sys_gfx_exit, sys_gfx_clear, sys_gfx_pixel, sys_gfx_color,
    sys_gfx_vsync, sys_gfx_blit,
    sys_putchar_at,
    sys_key_down,
};

const syscalls_t* program_template(void) { return &g_template; }

static char  g_arg_storage[ARG_BUF_SIZE];
static char* g_argv[MAX_ARGS];

static void build_fname(const char* name, char* out) {
    int i = 0;
    while (name[i] && i < 8) { out[i] = name[i]; i++; }
    out[i++] = '.'; out[i++] = 'C'; out[i++] = 'O'; out[i++] = 'M';
    out[i] = '\0';
}

int program_run_args(const char* name, int argc, char** argv) {
    char fname[16];
    build_fname(name, fname);

    u8* loadp = (u8*)PROG_LOAD_ADDR;
    for (unsigned int i = 0; i < PROG_MAX_SIZE; i++) loadp[i] = 0;

    char* p = g_arg_storage;
    int remaining = ARG_BUF_SIZE - 1;
    int stored = 0;
    for (int i = 0; i < argc && i < MAX_ARGS && remaining > 1; i++) {
        g_argv[i] = p;
        const char* s = argv[i] ? argv[i] : "";
        while (*s && remaining > 1) { *p++ = *s++; remaining--; }
        *p++ = '\0'; remaining--; stored++;
    }

    syscalls_t s;
    s = g_template;
    s.argc = stored;
    s.argv = g_argv;

    int n = fat12_cwd_read(fname, (u8*)PROG_LOAD_ADDR, PROG_MAX_SIZE);
    if (n <= 0) {
        /* Try \PROGRAMS\NAME.COM */
        char pname[32];
        int k = 0;
        const char* pre = "\\PROGRAMS\\";
        while (pre[k] && k < 30) { pname[k] = pre[k]; k++; }
        int m = 0;
        while (fname[m] && k < 31) pname[k++] = fname[m++];
        pname[k] = '\0';
        n = fat12_cwd_read(pname, (u8*)PROG_LOAD_ADDR, PROG_MAX_SIZE);
    }
    if (n <= 0) return -1;

    typedef void (*entry_t)(syscalls_t*);
    entry_t entry = (entry_t)PROG_LOAD_ADDR;
    entry(&s);
    return 0;
}

int program_run(const char* name) {
    return program_run_args(name, 0, 0);
}

static int g_slot_used[MAX_PROG_SLOTS];

int program_alloc_slot(void) {
    for (int i = 0; i < MAX_PROG_SLOTS; i++) {
        if (!g_slot_used[i]) { g_slot_used[i] = 1; return i; }
    }
    return -1;
}
void program_free_slot(int slot) {
    if (slot >= 0 && slot < MAX_PROG_SLOTS) g_slot_used[slot] = 0;
}
void* program_slot_addr(int slot) {
    if (slot < 0 || slot >= MAX_PROG_SLOTS) return 0;
    return (void*)(PROG_SLOT_BASE + (unsigned int)slot * PROG_SLOT_STRIDE);
}
int program_load_into_slot(const char* name, int slot) {
    void* addr = program_slot_addr(slot);
    if (!addr) return -1;
    char fname[16];
    build_fname(name, fname);
    u8* p = (u8*)addr;
    for (unsigned int i = 0; i < PROG_SLOT_STRIDE; i++) p[i] = 0;
    return fat12_cwd_read(fname, (u8*)addr, PROG_SLOT_STRIDE);
}
