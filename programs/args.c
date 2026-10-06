#include "program.h"

static void put_dec(syscalls_t* sys, int v) {
    if (v == 0) { sys->putchar('0'); return; }
    if (v < 0) { sys->putchar('-'); v = -v; }
    char tmp[12]; int n = 0;
    while (v > 0) { tmp[n++] = '0' + (v % 10); v /= 10; }
    while (n > 0) sys->putchar(tmp[--n]);
}

__attribute__((section(".text.entry")))
void _start(syscalls_t* sys) {
    sys->set_color(0x0E);
    sys->print("\n=== ARGS.COM ===\n");
    sys->set_color(0x07);

    sys->print("argc = ");
    put_dec(sys, sys->argc);
    sys->print("\n\n");

    for (int i = 0; i < sys->argc; i++) {
        sys->print("argv[");
        put_dec(sys, i);
        sys->print("] = \"");
        sys->print(sys->argv[i]);
        sys->print("\"\n");
    }
    sys->print("\n");
}
