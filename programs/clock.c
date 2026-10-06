#include "program.h"

#define CLOCK_X  65      /* top-right, clear of the banner text */
#define CLOCK_Y  0
#define INTERVAL 1000

static void put2(syscalls_t* sys, int x, int y, int v, unsigned char color) {
    sys->putchar_at(x,     y, '0' + (v / 10), color);
    sys->putchar_at(x + 1, y, '0' + (v % 10), color);
}

__attribute__((section(".text.entry")))
void _start(syscalls_t* sys) {
    unsigned char color = 0x0E;    /* yellow */

    while (1) {
        int h = 0, m = 0, s = 0;
        sys->get_rtc(&h, &m, &s);

        int x = CLOCK_X;
        sys->putchar_at(x++, CLOCK_Y, '[',           color);
        put2(sys, x, CLOCK_Y, h, color); x += 2;
        sys->putchar_at(x++, CLOCK_Y, ':',           color);
        put2(sys, x, CLOCK_Y, m, color); x += 2;
        sys->putchar_at(x++, CLOCK_Y, ':',           color);
        put2(sys, x, CLOCK_Y, s, color); x += 2;
        sys->putchar_at(x++, CLOCK_Y, ']',           color);

        sys->sleep_ms(INTERVAL);
    }
}
