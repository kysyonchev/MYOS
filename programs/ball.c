#include "program.h"

#define W 320
#define H 200
#define FB_BYTES (W * H)
#define MOVE_EVERY 2   /* update position every Nth frame (60/N fps effective) */

static unsigned char* shadow;

static void px(int x, int y, unsigned char c) {
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    shadow[y * W + x] = c;
}

static void clear_shadow(unsigned char c) {
    unsigned char* p = shadow;
    unsigned int n = FB_BYTES;
    while (n >= 4) { p[0]=c; p[1]=c; p[2]=c; p[3]=c; p += 4; n -= 4; }
    while (n--) *p++ = c;
}

static void draw_disc(int cx, int cy, int r, unsigned char color) {
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x*x + y*y <= r*r) px(cx + x, cy + y, color);
        }
    }
}

__attribute__((section(".text.entry")))
void _start(syscalls_t* sys) {
    sys->gfx_init();

    shadow = (unsigned char*)sys->malloc(FB_BYTES);
    if (!shadow) { sys->gfx_exit(); while (1) {} }

    int x = 160, y = 100, dx = 1, dy = 1;
    int frame = 0;
    unsigned int end_tick = sys->ticks() + 1500;    /* ~15 seconds */

    while (sys->ticks() < end_tick) {
        clear_shadow(1);

        /* Bottom color band */
        for (int bx = 0; bx < W; bx++) {
            unsigned char c = (unsigned char)(32 + (bx / 10) % 32);
            for (int by = H - 20; by < H; by++) {
                shadow[by * W + bx] = c;
            }
        }

        draw_disc(x - dx * 4, y - dy * 4, 6, 8);
        draw_disc(x, y, 6, 14);

        sys->gfx_vsync();
        sys->gfx_blit(shadow);

        /* Update position every MOVE_EVERY frames */
        if (++frame >= MOVE_EVERY) {
            frame = 0;
            x += dx; y += dy;
            if (x < 6 || x > W - 7) dx = -dx;
            if (y < 6 || y > H - 27) dy = -dy;
        }

        if (sys->kbhit()) { (void)sys->getchar(); break; }
    }

    sys->gfx_exit();
    while (1) { }
}
