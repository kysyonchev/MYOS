#include "memory.h"
#include "screen.h"

#define HEAP_START   0x00400000u
#define HEAP_END     0x00800000u
#define HEAP_SIZE    (HEAP_END - HEAP_START)
#define ALIGN        16
#define MAGIC_USED   0x55534544u

typedef struct {
    unsigned int size;
    unsigned int magic;
} block_t;

static unsigned char* g_bump       = 0;
static unsigned int   g_total      = 0;
static unsigned int   g_used       = 0;
static unsigned int   g_used_blocks = 0;

static unsigned int align_up(unsigned int v) {
    return (v + (ALIGN - 1)) & ~(ALIGN - 1);
}

void memory_init(void) {
    g_bump        = (unsigned char*)HEAP_START;
    g_total       = HEAP_SIZE;
    g_used        = 0;
    g_used_blocks = 0;
}

void* kmalloc(unsigned int size) {
    if (size == 0) return 0;
    unsigned int need  = align_up(size);
    unsigned int total = need + sizeof(block_t);

    if ((unsigned int)g_bump + total > HEAP_END) return 0;

    block_t* b = (block_t*)g_bump;
    b->size  = need;
    b->magic = MAGIC_USED;

    void* payload = g_bump + sizeof(block_t);
    g_bump    += total;
    g_used    += total;
    g_used_blocks++;
    return payload;
}

void kfree(void* ptr) {
    /* Bump allocator: reclaiming is a no-op.  Fine for now. */
    (void)ptr;
}

static void put_u32(unsigned int v) {
    char tmp[12]; int n = 0;
    if (v == 0) { terminal_putchar('0'); return; }
    while (v > 0) { tmp[n++] = '0' + (v % 10); v /= 10; }
    while (n > 0) terminal_putchar(tmp[--n]);
}

static void put_hex32(unsigned int v) {
    const char* hx = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--) {
        terminal_putchar(hx[(v >> (i * 4)) & 0xF]);
    }
}

void memory_print_stats(void) {
    terminal_write("\n");
    terminal_write_color("KERNEL HEAP\n\n", VGA_COLOR_LIGHT_CYAN);
    terminal_write("  Range       : 0x00400000 - 0x00800000\n");
    terminal_write("  Total       : ");
    put_u32(g_total / 1024);
    terminal_write(" KB\n");

    terminal_write("  Used        : ");
    put_u32(g_used / 1024);
    terminal_write(" KB\n");

    terminal_write("  Free        : ");
    put_u32((g_total - g_used) / 1024);
    terminal_write(" KB\n");

    terminal_write("  Live blocks : ");
    put_u32(g_used_blocks);
    terminal_write("\n");

    terminal_write("  Bump ptr    : 0x");
    put_hex32((unsigned int)g_bump);
    terminal_write("\n\n");
}

void memory_dump_freelist(void) {
    terminal_write("\n");
    terminal_write_color("HEAP STATE (bump allocator)\n\n", VGA_COLOR_LIGHT_CYAN);
    terminal_write("  Base     : 0x");
    put_hex32(HEAP_START);
    terminal_write("\n  Bump     : 0x");
    put_hex32((unsigned int)g_bump);
    terminal_write("\n  End      : 0x");
    put_hex32(HEAP_END);
    terminal_write("\n  Used     : ");
    put_u32(g_used);
    terminal_write(" bytes in ");
    put_u32(g_used_blocks);
    terminal_write(" blocks\n\n");
}
