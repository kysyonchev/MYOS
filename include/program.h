#ifndef PROGRAM_H
#define PROGRAM_H

#define PROG_LOAD_ADDR  0x00200000
#define PROG_MAX_SIZE   (128 * 1024)

#define MAX_PROG_SLOTS      4
#define PROG_SLOT_BASE      0x00300000
#define PROG_SLOT_STRIDE    0x00020000

typedef struct {
    void (*print)(const char* str);
    void (*putchar)(char c);
    void (*clear)(void);
    void (*set_color)(unsigned char color);
    char (*getchar)(void);
    void (*exit)(void);
    int    argc;
    char** argv;
    void* (*malloc)(unsigned int size);
    void  (*free)(void* ptr);
    int   (*file_load)(const char* name, char* buf, unsigned int max);
    int   (*file_save)(const char* name, const char* buf, unsigned int size);
    int   (*file_exists)(const char* name);
    unsigned int (*file_size)(const char* name);
    void          (*gotoxy)(int x, int y);
    unsigned int  (*ticks)(void);
    unsigned int  (*rand)(void);
    int           (*kbhit)(void);
    /* v5 */
    void (*sleep_ms)(unsigned int ms);
    void (*get_cursor)(int* x, int* y);
    void (*get_rtc)(int* h, int* m, int* s);
    /* v6 */
    void (*gfx_init)(void);
    void (*gfx_exit)(void);
    void (*gfx_clear)(unsigned char color);
    void (*gfx_pixel)(int x, int y, unsigned char color);
    void (*gfx_color)(unsigned char idx, unsigned char r, unsigned char g, unsigned char b);
    /* v7 */
    void (*gfx_vsync)(void);
    void (*gfx_blit)(const unsigned char* buffer);
    /* v8 */
    void (*putchar_at)(int x, int y, char c, unsigned char color);
    /* v9 */
    int  (*key_down)(char c);
} syscalls_t;

int program_run(const char* name);
int program_run_args(const char* name, int argc, char** argv);

/* Slot management for background programs */
int   program_alloc_slot(void);
void  program_free_slot(int slot);
void* program_slot_addr(int slot);
int   program_load_into_slot(const char* name, int slot);

/* Template for per-task syscall tables */
const syscalls_t* program_template(void);

#endif
