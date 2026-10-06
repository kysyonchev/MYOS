#include "program.h"

static int parse_int(const char* s) {
    int sign = 1, v = 0;
    if (*s == '-') { sign = -1; s++; }
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return v * sign;
}

static int read_int(syscalls_t* sys, const char* prompt) {
    sys->print(prompt);
    char buf[16];
    int n = 0;
    while (1) {
        char c = sys->getchar();
        if (c == '\n') break;
        if (c == '\b') {
            if (n > 0) { n--; sys->putchar('\b'); }
            continue;
        }
        if (c < '0' || c > '9') continue;
        if (n < 15) { buf[n++] = c; sys->putchar(c); }
    }
    buf[n] = '\0';
    sys->print("\n");

    int v = 0;
    for (int i = 0; i < n; i++) v = v * 10 + (buf[i] - '0');
    return v;
}

static void print_int(syscalls_t* sys, int v) {
    if (v == 0) { sys->putchar('0'); return; }
    if (v < 0) { sys->putchar('-'); v = -v; }
    char tmp[12]; int t = 0;
    while (v > 0) { tmp[t++] = '0' + (v % 10); v /= 10; }
    while (t > 0) sys->putchar(tmp[--t]);
}

__attribute__((section(".text.entry")))
void _start(syscalls_t* sys) {
    sys->set_color(0x0B);
    sys->print("\n=== CALC.COM ===\n");
    sys->set_color(0x07);

    int a, b, interactive;

    if (sys->argc >= 3) {
        a = parse_int(sys->argv[1]);
        b = parse_int(sys->argv[2]);
        interactive = 0;
        sys->print("a = "); print_int(sys, a); sys->print("\n");
        sys->print("b = "); print_int(sys, b); sys->print("\n\n");
    } else {
        a = read_int(sys, "First number:  ");
        b = read_int(sys, "Second number: ");
        interactive = 1;
    }

    sys->print("Sum:  "); print_int(sys, a + b); sys->print("\n");
    sys->print("Diff: "); print_int(sys, a - b); sys->print("\n");
    sys->print("Prod: "); print_int(sys, a * b); sys->print("\n\n");

    if (interactive) {
        sys->print("Press any key to return...");
        sys->getchar();
    }
    sys->print("\n");
}
