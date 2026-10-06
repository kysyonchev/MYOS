#include "program.h"

__attribute__((section(".text.entry")))
void _start(syscalls_t* sys) {
    sys->print("\n=== ECHO.COM ===\n");
    sys->print("Type a line.  Press Enter when done.\n");
    sys->print("> ");

    char buf[128];
    int n = 0;

    while (1) {
        char c = sys->getchar();
        if (c == '\n') break;
        if (c == '\b') {
            if (n > 0) { n--; sys->putchar('\b'); }
            continue;
        }
        if (c < 32 || c > 126) continue;
        if (n < 127) {
            buf[n++] = c;
            sys->putchar(c);
        }
    }
    buf[n] = '\0';

    sys->print("\nYou typed: ");
    sys->print(buf);
    sys->print("\n");
}
