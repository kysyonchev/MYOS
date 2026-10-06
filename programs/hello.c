#include "program.h"

__attribute__((section(".text.entry")))
void _start(syscalls_t* sys) {
    sys->set_color(0x0A);
    sys->print("\n=== HELLO.COM ===\n");
    sys->set_color(0x07);
    sys->print("This program was loaded from disk\n");
    sys->print("and is running at address 0x200000.\n\n");

    if (sys->argc > 1) {
        sys->print("(You passed ");
        for (int i = 0; i < sys->argc; i++) {
            sys->print("\"");
            sys->print(sys->argv[i]);
            sys->print("\" ");
        }
        sys->print("-- try ARGS for a full listing.)\n\n");
    }

    sys->print("Press any key to return to the shell...");
    sys->getchar();
    sys->print("\n");
}
