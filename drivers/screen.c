#include "screen.h"

static unsigned short* const vga_buffer = (unsigned short*)VGA_MEMORY;
static int terminal_row = 0;
static int terminal_column = 0;
static unsigned char terminal_color = VGA_COLOR_LIGHT_GREY | (VGA_COLOR_BLACK << 4);

// Helper to create a VGA entry
static unsigned short vga_entry(unsigned char c, unsigned char color) {
    return (unsigned short)c | ((unsigned short)color << 8);
}

#define SCROLL_MAX 512

static unsigned short scroll_buf[SCROLL_MAX][VGA_WIDTH];
static int  scroll_count = 0;
static int  scroll_head  = 0;
static int  scroll_view  = 0;
static unsigned short live_save[VGA_HEIGHT * VGA_WIDTH];
static int  live_saved = 0;

static void set_hw_cursor(int x, int y) {
    unsigned short pos = (unsigned short)(y * VGA_WIDTH + x);
    __asm__ volatile ("outb %0, %1" : : "a"((unsigned char)0x0F), "Nd"((unsigned short)0x3D4));
    __asm__ volatile ("outb %0, %1" : : "a"((unsigned char)(pos & 0xFF)), "Nd"((unsigned short)0x3D5));
    __asm__ volatile ("outb %0, %1" : : "a"((unsigned char)0x0E), "Nd"((unsigned short)0x3D4));
    __asm__ volatile ("outb %0, %1" : : "a"((unsigned char)((pos >> 8) & 0xFF)), "Nd"((unsigned short)0x3D5));
}

static void scroll_buf_push_row(int row) {
    for (int i = 0; i < VGA_WIDTH; i++) {
        scroll_buf[scroll_head][i] = vga_buffer[row * VGA_WIDTH + i];
    }
    scroll_head = (scroll_head + 1) % SCROLL_MAX;
    if (scroll_count < SCROLL_MAX) scroll_count++;
}

static void terminal_render_scroll(int offset) {
    int visible_end   = scroll_count - offset;
    int visible_start = visible_end - VGA_HEIGHT;
    if (visible_start < 0) visible_start = 0;
    if (visible_end   > scroll_count) visible_end = scroll_count;

    for (int y = 0; y < VGA_HEIGHT; y++) {
        int line_idx = visible_start + y;
        for (int x = 0; x < VGA_WIDTH; x++) {
            unsigned short cell = 0x0720;    /* space, light grey */
            if (line_idx >= 0 && line_idx < visible_end) {
                int ring = (scroll_head - scroll_count + line_idx + SCROLL_MAX * 2) % SCROLL_MAX;
                cell = scroll_buf[ring][x];
            }
            vga_buffer[y * VGA_WIDTH + x] = cell;
        }
    }
    set_hw_cursor(0, 0);
}

void terminal_scroll_up(void) {
    if (scroll_count == 0) return;
    int max_offset = scroll_count > VGA_HEIGHT ? scroll_count - VGA_HEIGHT : 0;
    if (scroll_view >= max_offset) return;

    if (scroll_view == 0) {
        for (int i = 0; i < VGA_HEIGHT * VGA_WIDTH; i++) live_save[i] = vga_buffer[i];
        live_saved = 1;
    }
    scroll_view++;
    terminal_render_scroll(scroll_view);
}

void terminal_scroll_down(void) {
    if (scroll_view == 0) return;
    scroll_view--;
    if (scroll_view == 0 && live_saved) {
        for (int i = 0; i < VGA_HEIGHT * VGA_WIDTH; i++) vga_buffer[i] = live_save[i];
        set_hw_cursor(terminal_column, terminal_row);
        live_saved = 0;
    } else {
        terminal_render_scroll(scroll_view);
    }
}

void terminal_scroll_reset(void) {
    if (scroll_view == 0) return;
    if (live_saved) {
        for (int i = 0; i < VGA_HEIGHT * VGA_WIDTH; i++) vga_buffer[i] = live_save[i];
        set_hw_cursor(terminal_column, terminal_row);
        live_saved = 0;
    }
    scroll_view = 0;
}

int terminal_scroll_view(void) { return scroll_view; }

void terminal_initialize(void) {
    terminal_row = 0;
    terminal_column = 0;
    terminal_color = VGA_COLOR_LIGHT_GREY | (VGA_COLOR_BLACK << 4);
    terminal_clear();
    terminal_set_cursor(0, 0);
}

void terminal_clear(void) {
    for (int y = 0; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[y * VGA_WIDTH + x] = vga_entry(' ', terminal_color);
        }
    }
    terminal_row = 0;
    terminal_column = 0;
    scroll_view = 0;
    live_saved = 0;
    terminal_set_cursor(0, 0);
}

void terminal_set_color(unsigned char color) {
    terminal_color = color;
}

unsigned char terminal_get_color(void) {
    return terminal_color;
}

static void terminal_scroll(void) {
    // Move everything up one line
    for (int y = 0; y < VGA_HEIGHT - 1; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[y * VGA_WIDTH + x] = vga_buffer[(y + 1) * VGA_WIDTH + x];
        }
    }
    // Clear the last line
    for (int x = 0; x < VGA_WIDTH; x++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', terminal_color);
    }
    terminal_row = VGA_HEIGHT - 1;
    terminal_column = 0;
}

void terminal_putchar(char c) {
    /* If we're scrolled back, snap to live first. */
    if (scroll_view != 0) terminal_scroll_reset();

    if (c == '\n') {
        scroll_buf_push_row(terminal_row);
        terminal_column = 0;
        terminal_row++;
    } else if (c == '\r') {
        terminal_column = 0;
    } else if (c == '\t') {
        for (int i = 0; i < 4; i++) terminal_putchar(' ');
        return;
    } else if (c == '\b') {
        if (terminal_column > 0) {
            terminal_column--;
        } else if (terminal_row > 0) {
            terminal_row--;
            terminal_column = VGA_WIDTH - 1;
        }
        vga_buffer[terminal_row * VGA_WIDTH + terminal_column] = vga_entry(' ', terminal_color);
        terminal_set_cursor(terminal_column, terminal_row);
        return;
    } else {
        vga_buffer[terminal_row * VGA_WIDTH + terminal_column] = vga_entry(c, terminal_color);
        terminal_column++;
    }

    if (terminal_column >= VGA_WIDTH) {
        scroll_buf_push_row(terminal_row);
        terminal_column = 0;
        terminal_row++;
    }

    if (terminal_row >= VGA_HEIGHT) {
        terminal_scroll();
    }

    terminal_set_cursor(terminal_column, terminal_row);
}

void terminal_write(const char* data) {
    for (int i = 0; data[i] != '\0'; i++) {
        terminal_putchar(data[i]);
    }
}

void terminal_write_color(const char* data, unsigned char color) {
    unsigned char old = terminal_color;
    terminal_set_color(color);
    terminal_write(data);
    terminal_set_color(old);
}

void terminal_set_cursor(int x, int y) {
    if (x < 0) x = 0;
    if (x >= VGA_WIDTH)  x = VGA_WIDTH  - 1;
    if (y < 0) y = 0;
    if (y >= VGA_HEIGHT) y = VGA_HEIGHT - 1;

    /* Update internal state so the next terminal_putchar goes to (x,y). */
    terminal_row    = y;
    terminal_column = x;

    unsigned short pos = (unsigned short)(y * VGA_WIDTH + x);
    __asm__ volatile ("outb %0, %1" : : "a"((unsigned char)0x0F), "Nd"((unsigned short)0x3D4));
    __asm__ volatile ("outb %0, %1" : : "a"((unsigned char)(pos & 0xFF)), "Nd"((unsigned short)0x3D5));
    __asm__ volatile ("outb %0, %1" : : "a"((unsigned char)0x0E), "Nd"((unsigned short)0x3D4));
    __asm__ volatile ("outb %0, %1" : : "a"((unsigned char)((pos >> 8) & 0xFF)), "Nd"((unsigned short)0x3D5));
}

void terminal_get_cursor(int* x, int* y) {
    *x = terminal_column;
    *y = terminal_row;
}