#ifndef PROG_API_H
#define PROG_API_H

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
    void (*sleep_ms)(unsigned int ms);
    void (*get_cursor)(int* x, int* y);
    void (*get_rtc)(int* h, int* m, int* s);
    void (*gfx_init)(void);
    void (*gfx_exit)(void);
    void (*gfx_clear)(unsigned char color);
    void (*gfx_pixel)(int x, int y, unsigned char color);
    void (*gfx_color)(unsigned char idx, unsigned char r, unsigned char g, unsigned char b);
    void (*gfx_vsync)(void);
    void (*gfx_blit)(const unsigned char* buffer);
    void (*putchar_at)(int x, int y, char c, unsigned char color);
    /* v9 */
    int  (*key_down)(char c);
} syscalls_t;

#define KEY_UP    ((char)0x80)
#define KEY_DOWN  ((char)0x81)
#define KEY_LEFT  ((char)0x82)
#define KEY_RIGHT ((char)0x83)

#endif
