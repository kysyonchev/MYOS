#include "program.h"

#define MAX_LINES 60
#define MAX_LINE_LEN 76

static char lines[MAX_LINES][MAX_LINE_LEN + 1];
static int  line_count = 0;
static int  modified = 0;
static char filename[32];

/* ---------- helpers ---------- */

static void put_dec(syscalls_t* sys, int v) {
    if (v == 0) { sys->putchar('0'); return; }
    if (v < 0) { sys->putchar('-'); v = -v; }
    char tmp[12]; int n = 0;
    while (v > 0) { tmp[n++] = '0' + (v % 10); v /= 10; }
    while (n > 0) sys->putchar(tmp[--n]);
}

static int parse_uint(const char* s, int* out) {
    int i = 0;
    while (s[i] == ' ') i++;
    if (s[i] < '0' || s[i] > '9') return 0;
    int v = 0;
    while (s[i] >= '0' && s[i] <= '9') { v = v * 10 + (s[i] - '0'); i++; }
    while (s[i] == ' ') i++;
    if (s[i] != '\0') return 0;
    *out = v;
    return 1;
}

static int read_line(syscalls_t* sys, char* buf, int max) {
    int n = 0;
    while (1) {
        char c = sys->getchar();
        if (c == '\n') break;
        if (c == '\b') {
            if (n > 0) { n--; sys->putchar('\b'); }
            continue;
        }
        if (c < 32 || c > 126) continue;
        if (n < max - 1) { buf[n++] = c; sys->putchar(c); }
    }
    buf[n] = '\0';
    sys->print("\n");
    return n;
}

/* ---------- file load / save ---------- */

static int load_file(syscalls_t* sys, const char* name) {
    if (!sys->file_exists(name)) return 0;

    unsigned int sz = sys->file_size(name);
    if (sz == 0) return 0;
    if (sz > 8192) sz = 8192;

    char* buf = (char*)sys->malloc(sz + 1);
    if (!buf) return -1;

    int n = sys->file_load(name, buf, sz);
    if (n < 0) { sys->free(buf); return -1; }

    int i = 0, line_start = 0;
    while (i < n && line_count < MAX_LINES) {
        if (buf[i] == '\n' || buf[i] == '\r') {
            int len = i - line_start;
            if (len > MAX_LINE_LEN) len = MAX_LINE_LEN;
            for (int j = 0; j < len; j++) lines[line_count][j] = buf[line_start + j];
            lines[line_count][len] = '\0';
            line_count++;
            if (buf[i] == '\r' && i + 1 < n && buf[i + 1] == '\n') i++;
            line_start = i + 1;
        }
        i++;
    }
    if (line_start < n && line_count < MAX_LINES) {
        int len = n - line_start;
        if (len > MAX_LINE_LEN) len = MAX_LINE_LEN;
        for (int j = 0; j < len; j++) lines[line_count][j] = buf[line_start + j];
        lines[line_count][len] = '\0';
        line_count++;
    }

    sys->free(buf);
    return 0;
}

static int save_file(syscalls_t* sys, const char* name) {
    unsigned int total = 0;
    for (int i = 0; i < line_count; i++) {
        int j = 0;
        while (lines[i][j]) j++;
        total += j + 2;
    }
    if (total == 0) total = 1;

    char* buf = (char*)sys->malloc(total);
    if (!buf) return -1;

    unsigned int p = 0;
    for (int i = 0; i < line_count; i++) {
        int j = 0;
        while (lines[i][j]) { buf[p++] = lines[i][j]; j++; }
        buf[p++] = '\r';
        buf[p++] = '\n';
    }

    int r = sys->file_save(name, buf, (int)p);
    sys->free(buf);
    return r;
}

/* ---------- commands ---------- */

static void list_lines(syscalls_t* sys) {
    sys->print("\n");
    if (line_count == 0) {
        sys->print("  (empty file)\n");
    } else {
        for (int i = 0; i < line_count; i++) {
            put_dec(sys, i + 1);
            sys->print(": ");
            sys->print(lines[i]);
            sys->print("\n");
        }
    }
    sys->print("\n");
}

static void edit_line_at(syscalls_t* sys, int n) {
    if (n < 1 || n > line_count) {
        sys->print("Line out of range.\n");
        return;
    }
    put_dec(sys, n);
    sys->print(": ");
    sys->print(lines[n - 1]);
    sys->print("\n");
    put_dec(sys, n);
    sys->print("> ");

    char buf[MAX_LINE_LEN + 1];
    int len = read_line(sys, buf, sizeof(buf));
    if (len == 0) return;
    int j = 0;
    while (buf[j]) { lines[n - 1][j] = buf[j]; j++; }
    lines[n - 1][j] = '\0';
    modified = 1;
}

static void append_line(syscalls_t* sys) {
    if (line_count >= MAX_LINES) {
        sys->print("File is full.\n");
        return;
    }
    sys->print("New line: ");
    char buf[MAX_LINE_LEN + 1];
    int len = read_line(sys, buf, sizeof(buf));
    if (len == 0) return;
    int j = 0;
    while (buf[j]) { lines[line_count][j] = buf[j]; j++; }
    lines[line_count][j] = '\0';
    line_count++;
    modified = 1;
}

static void delete_line_at(syscalls_t* sys, int n) {
    if (n < 1 || n > line_count) {
        sys->print("Line out of range.\n");
        return;
    }
    for (int i = n - 1; i < line_count - 1; i++) {
        int j = 0;
        while (lines[i + 1][j]) { lines[i][j] = lines[i + 1][j]; j++; }
        lines[i][j] = '\0';
    }
    line_count--;
    modified = 1;
    sys->print("Line deleted.\n");
}

static void print_help(syscalls_t* sys) {
    sys->print("\nCommands:\n");
    sys->print("  L            list all lines\n");
    sys->print("  n            edit line n (e.g. '2' to edit line 2)\n");
    sys->print("  A            append a new line at the end\n");
    sys->print("  D n          delete line n\n");
    sys->print("  W            write file to disk\n");
    sys->print("  Q            quit (with save prompt)\n");
    sys->print("  H            this help\n\n");
}

/* ---------- main ---------- */

__attribute__((section(".text.entry")))
void _start(syscalls_t* sys) {
    sys->set_color(0x0B);
    sys->print("\n=== EDIT.COM ===\n");
    sys->set_color(0x07);

    if (sys->argc < 2) {
        sys->print("Usage: EDIT <filename>\n");
        sys->print("Press any key...\n");
        sys->getchar();
        return;
    }

    int i = 0;
    while (sys->argv[1][i] && i < 31) { filename[i] = sys->argv[1][i]; i++; }
    filename[i] = '\0';

    sys->print("Editing: ");
    sys->print(filename);
    sys->print("\n");

    if (load_file(sys, filename) < 0) {
        sys->print("Could not load file.\n");
        return;
    }
    sys->print("Loaded ");
    put_dec(sys, line_count);
    sys->print(" line(s).\n");

    print_help(sys);

    while (1) {
        sys->print("* ");
        char cmd[80];
        read_line(sys, cmd, sizeof(cmd));

        if (cmd[0] == '\0') continue;

        char c0 = cmd[0];
        if (c0 >= 'a' && c0 <= 'z') c0 -= 32;

        if (c0 == 'L' && cmd[1] == '\0') {
            list_lines(sys);
        } else if (c0 == 'H' && cmd[1] == '\0') {
            print_help(sys);
        } else if (c0 == 'A' && cmd[1] == '\0') {
            append_line(sys);
        } else if (c0 == 'W' && cmd[1] == '\0') {
            if (save_file(sys, filename) < 0) {
                sys->print("Save failed.\n");
            } else {
                sys->print("Saved ");
                put_dec(sys, line_count);
                sys->print(" line(s) to ");
                sys->print(filename);
                sys->print(".\n");
                modified = 0;
            }
        } else if (c0 == 'Q' && cmd[1] == '\0') {
            if (modified) {
                sys->print("Unsaved changes. Save first? (y/n) ");
                char ans[8];
                read_line(sys, ans, sizeof(ans));
                if (ans[0] == 'y' || ans[0] == 'Y') {
                    if (save_file(sys, filename) >= 0) sys->print("Saved.\n");
                }
            }
            sys->print("Goodbye.\n\n");
            return;
        } else if (c0 == 'D' && cmd[1] == ' ') {
            int n;
            if (parse_uint(cmd + 2, &n)) {
                delete_line_at(sys, n);
            } else {
                sys->print("Usage: D <n>\n");
            }
        } else {
            int n;
            if (parse_uint(cmd, &n)) {
                edit_line_at(sys, n);
            } else {
                sys->print("Unknown command. Press H for help.\n");
            }
        }
    }
}
