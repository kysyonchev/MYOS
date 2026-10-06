#include "shell.h"
#include "screen.h"
#include "keyboard.h"
#include "fat12.h"
#include "program.h"
#include "interrupts.h"
#include "memory.h"
#include "task.h"
#include "settings.h"
#include "env.h"

#define MAX_COMMAND 128
#define MAX_BATCH_DEPTH 4

/* Arrow / editing key codes returned by keyboard_getchar() */
#define KEY_UP        ((char)0x80)
#define KEY_DOWN      ((char)0x81)
#define KEY_LEFT      ((char)0x82)
#define KEY_RIGHT     ((char)0x83)
#define KEY_DEL       ((char)0x84)
#define KEY_HOME      ((char)0x85)
#define KEY_END       ((char)0x86)
#define KEY_CTRL_UP   ((char)0x88)
#define KEY_CTRL_DOWN ((char)0x89)

/* Command history */
#define HIST_MAX      16
#define HIST_LINE_MAX 128

static char command_buffer[MAX_COMMAND];
static int  command_length = 0;
static int  command_cursor = 0;
static char cwd_display[64];

static int  g_batch_depth = 0;
static int  g_batch_echo  = 1;

static char g_history[HIST_MAX][HIST_LINE_MAX];
static int  g_hist_count  = 0;   /* how many valid entries */
static int  g_hist_head   = 0;   /* next write slot */
static int  g_hist_view   = -1;  /* -1 = editing fresh, 0 = newest */

/* ---------- small helpers ---------- */

static int str_eq_ci(const char* a, const char* b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

static void to_upper(char* s) {
    for (int i = 0; s[i]; i++) {
        if (s[i] >= 'a' && s[i] <= 'z') s[i] -= 32;
    }
}

static int split_args(char* line, char** a1, char** a2) {
    *a1 = 0; *a2 = 0;
    int n = 0;
    char* p = line;
    while (*p == ' ') p++;
    if (*p == '\0') return 0;
    *a1 = p; n = 1;
    while (*p && *p != ' ') p++;
    if (*p == '\0') return n;
    *p++ = '\0';
    while (*p == ' ') p++;
    if (*p == '\0') return n;
    *a2 = p; n = 2;
    while (*p && *p != ' ') p++;
    *p = '\0';
    return n;
}

static inline unsigned char inb(unsigned short port) {
    unsigned char r;
    __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}
static inline void outb(unsigned short port, unsigned char v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}

static unsigned char bcd_to_bin(unsigned char v) {
    return (v & 0x0F) + ((v >> 4) * 10);
}
static void read_rtc(int* h, int* m, int* s) {
    outb(0x70, 0x00); unsigned char sec = inb(0x71);
    outb(0x70, 0x02); unsigned char min = inb(0x71);
    outb(0x70, 0x04); unsigned char hr  = inb(0x71);
    *s = bcd_to_bin(sec);
    *m = bcd_to_bin(min);
    *h = bcd_to_bin(hr);
}

static void refresh_cwd_display(void) {
    fat12_cwd_path(cwd_display, sizeof(cwd_display));
}

/* ---------- banners and help ---------- */

static void print_banner(void) {
    terminal_clear();
    settings_t* s = settings_get();
    terminal_write_color(s->hostname, VGA_COLOR_LIGHT_CYAN);
    terminal_write_color(" DOS\n", VGA_COLOR_LIGHT_CYAN);
    terminal_write("Version 0.6\n");
    terminal_write("Type HELP for a list of commands.\n\n");
    terminal_set_color(s->text_color);
}

static void cmd_help(void) {
    terminal_write("\n");
    terminal_write_color("MYOS COMMANDS\n\n", VGA_COLOR_LIGHT_CYAN);
    terminal_write("HELP               Show this help\n");
    terminal_write("CLS                Clear the screen\n");
    terminal_write("DIR [path]         List files\n");
    terminal_write("MD  <name>         Make a directory\n");
    terminal_write("CD  <name>         Change directory\n");
    terminal_write("RD  <name>         Remove an empty directory\n");
    terminal_write("TYPE <file>        Show file contents\n");
    terminal_write("COPY <src> <dst>   Copy a file\n");
    terminal_write("DEL <file>         Delete a file\n");
    terminal_write("REN <old> <new>    Rename a file\n");
    terminal_write("RUN <file>         Run a .COM program\n");
    terminal_write("BAT <file>         Run a batch file\n");
    terminal_write("FIND <file>        Search for a file, print full path\n");
    terminal_write("ECHO <text>        Print text\n");
    terminal_write("ENV [N=V | N | -N] List, set, show or unset variables\n");
    terminal_write("SET [key value]    Show or change system settings\n");
    terminal_write("CONFIG             Show raw MYOS.CFG\n");
    terminal_write("VER                Show OS version\n");
    terminal_write("ABOUT              About MYOS\n");
    terminal_write("TIME               Show current time\n");
    terminal_write("TICKS              Show timer tick count\n");
    terminal_write("SLEEP <ms>         Sleep N milliseconds\n");
    terminal_write("MEM                Show kernel heap usage\n");
    terminal_write("TASKS              List running tasks\n");
    terminal_write("BG <prog>          Run a program in the background\n");
    terminal_write("KILL <pid>         Terminate a task\n");
    terminal_write("REBOOT             Restart the machine\n");
    terminal_write("SHUTDOWN           Power off the machine\n");
    terminal_write("\n");
    terminal_write("Environment variables: %NAME% expands in any command.\n");
    terminal_write("Batch files: .BAT text files run with BAT <name>.\n");
    terminal_write("Boot script: \\AUTOEXEC.BAT runs at every boot.\n\n");
}

static void cmd_ver(void) {
    terminal_write("\nMYOS DOS\n");
    terminal_write("Version 0.6\n\n");
}

static void cmd_about(void) {
    terminal_write("\n");
    terminal_write_color("MYOS\n", VGA_COLOR_LIGHT_GREEN);
    terminal_write("A small DOS-like operating system\n");
    terminal_write("written from scratch.\n\n");
    terminal_write("Kernel     : 32-bit x86, freestanding C\n");
    terminal_write("Interrupts : IDT + PIC + PIT (100 Hz)\n");
    terminal_write("Tasking    : Preemptive round-robin\n");
    terminal_write("Memory     : Bump allocator\n");
    terminal_write("Disk       : ATA PIO + FAT12 (read/write)\n");
    terminal_write("Programs   : .COM flat binaries + syscall table v5\n");
    terminal_write("Shell      : Built-ins + .BAT scripts + env vars\n");
    terminal_write("Settings   : Persistent in \\MYOS.CFG\n\n");
}

static void cmd_time(void) {
    int h, m, s;
    read_rtc(&h, &m, &s);
    char buf[16];
    int i = 0;
    buf[i++] = '0' + (h / 10);
    buf[i++] = '0' + (h % 10);
    buf[i++] = ':';
    buf[i++] = '0' + (m / 10);
    buf[i++] = '0' + (m % 10);
    buf[i++] = ':';
    buf[i++] = '0' + (s / 10);
    buf[i++] = '0' + (s % 10);
    buf[i] = '\0';
    terminal_write("\nCurrent time: ");
    terminal_write(buf);
    terminal_write("\n\n");
}

static void cmd_ticks(void) {
    unsigned int t = timer_ticks();
    char buf[16]; int n = 0;
    if (t == 0) buf[n++] = '0';
    else { char tmp[16]; int tt = 0;
           while (t > 0) { tmp[tt++] = '0' + (t % 10); t /= 10; }
           while (tt > 0) buf[n++] = tmp[--tt]; }
    buf[n] = '\0';
    terminal_write("\nTicks: ");
    terminal_write(buf);
    terminal_write("  (~");
    unsigned int secs = timer_ticks() / 100;
    char sb[8]; int sn = 0;
    if (secs == 0) sb[sn++] = '0';
    else { char tmp2[8]; int tt2 = 0;
           while (secs > 0) { tmp2[tt2++] = '0' + (secs % 10); secs /= 10; }
           while (tt2 > 0) sb[sn++] = tmp2[--tt2]; }
    sb[sn] = '\0';
    terminal_write(sb);
    terminal_write(" seconds)\n\n");
}

static void cmd_reboot(void) {
    terminal_write("\nRebooting...\n");
    for (volatile int i = 0; i < 50000000; i++) { }
    outb(0xCF9, 0x02);
    outb(0xCF9, 0x06);
    while (1) { __asm__ volatile ("hlt"); }
}

static inline void outw(unsigned short port, unsigned short v) {
    __asm__ volatile ("outw %0, %1" : : "a"(v), "Nd"(port));
}

static void cmd_shutdown(void) {
    terminal_write("\nShutting down...\n");
    for (volatile int i = 0; i < 20000000; i++) { }
    outw(0xB004, 0x2000);
    outw(0x604,  0x2000);
    terminal_write("\nIt is now safe to turn off your computer.\n");
    while (1) { __asm__ volatile ("cli; hlt"); }
}

/* ---------- file commands ---------- */

static void cmd_dir(char* args) {
    terminal_write("\n Volume in drive C is MYOS DISK\n");
    if (args && args[0] != '\0') {
        terminal_write(" Directory of C:");
        if (args[0] != '\\') terminal_write(cwd_display);
        terminal_write(args);
        terminal_write("\n\n");
        fat12_cwd_print_entries_at(args);
    } else {
        terminal_write(" Directory of C:");
        terminal_write(cwd_display);
        terminal_write("\n\n");
        fat12_cwd_print_entries();
    }
}

static void cmd_md(char* args) {
    if (!args || args[0] == '\0') { terminal_write("\nUsage: MD <name>\n\n"); return; }
    int r = fat12_cwd_mkdir(args);
    if      (r == -12) terminal_write("\nAlready exists: ");
    else if (r == -11) terminal_write("\nParent directory not found.\n\n");
    else if (r == -14) terminal_write("\nDisk full.\n\n");
    else if (r < 0)    terminal_write("\nFailed to create directory.\n\n");
    else { terminal_write("\nDirectory created.\n\n"); return; }
    if (r == -12) { terminal_write(args); terminal_write("\n\n"); }
}

static void cmd_cd(char* args) {
    if (!args || args[0] == '\0') {
        terminal_write("\nCurrent directory: C:");
        terminal_write(cwd_display);
        terminal_write("\n\n");
        return;
    }
    int r = fat12_cwd_chdir(args);
    if      (r == -1) terminal_write("\nDirectory not found: ");
    else if (r == -2) terminal_write("\nNot a directory: ");
    else if (r < 0)   { terminal_write("\nFailed to change directory.\n\n"); return; }
    else { refresh_cwd_display(); return; }
    terminal_write(args);
    terminal_write("\n\n");
}

static void cmd_rd(char* args) {
    if (!args || args[0] == '\0') { terminal_write("\nUsage: RD <name>\n\n"); return; }
    int r = fat12_cwd_rmdir(args);
    if      (r == -1) terminal_write("\nDirectory not found: ");
    else if (r == -2) terminal_write("\nNot a directory: ");
    else if (r == -5) terminal_write("\nDirectory not empty.\n\n");
    else if (r < 0)   terminal_write("\nFailed to remove directory.\n\n");
    else { terminal_write("\nDirectory removed.\n\n"); return; }
    if (r == -1 || r == -2) { terminal_write(args); terminal_write("\n\n"); }
}

static void put_dec_dbg(int v) {
    if (v == 0) { terminal_putchar('0'); return; }
    if (v < 0) { terminal_putchar('-'); v = -v; }
    char tmp[12]; int n = 0;
    while (v > 0) { tmp[n++] = '0' + (v % 10); v /= 10; }
    while (n > 0) terminal_putchar(tmp[--n]);
}

static void cmd_type(char* args) {
    if (!args || args[0] == '\0') { terminal_write("\nUsage: TYPE <filename>\n\n"); return; }

    u32 size = fat12_cwd_size(args);
    if (size == 0 && !fat12_cwd_exists(args)) {
        terminal_write("\nFile not found: ");
        terminal_write(args);
        terminal_write("\n\n");
        return;
    }
    if (size == 0) { terminal_write("\n(empty file)\n\n"); return; }

    u8* buf = (u8*)kmalloc(size + 1);
    if (!buf) { terminal_write("\nOut of memory.\n\n"); return; }

    int n = fat12_cwd_read(args, buf, size);
    if (n < 0) { terminal_write("\nRead error.\n\n"); kfree(buf); return; }

    terminal_write("\n");
    for (int i = 0; i < n; i++) terminal_putchar((char)buf[i]);
    terminal_write("\n");
    kfree(buf);
}

static void cmd_copy(char* a1, char* a2) {
    if (!a1 || !a2) { terminal_write("\nUsage: COPY <src> <dst>\n\n"); return; }
    u32 size = fat12_cwd_size(a1);
    if (size == 0 && !fat12_cwd_exists(a1)) {
        terminal_write("\nSource not found: ");
        terminal_write(a1);
        terminal_write("\n\n");
        return;
    }
    u8* buf = (u8*)kmalloc(size + 1);
    if (!buf) { terminal_write("\nOut of memory.\n\n"); return; }
    int n = fat12_cwd_read(a1, buf, size);
    if (n < 0) {
        terminal_write("\nSource not found.\n\n");
        kfree(buf);
        return;
    }
    int r = fat12_cwd_write(a2, buf, (u32)n);
    kfree(buf);
    if      (r == -1) terminal_write("\nDestination already exists.\n\n");
    else if (r < 0)   terminal_write("\nCopy failed (disk full?).\n\n");
    else              terminal_write("\n1 file(s) copied.\n\n");
}

static void cmd_del(char* args) {
    if (!args || args[0] == '\0') { terminal_write("\nUsage: DEL <filename>\n\n"); return; }
    int r = fat12_cwd_delete(args);
    if (r < 0) {
        terminal_write("\nFile not found: ");
        terminal_write(args);
        terminal_write("\n\n");
    } else terminal_write("\nFile deleted.\n\n");
}

static void cmd_ren(char* a1, char* a2) {
    if (!a1 || !a2) { terminal_write("\nUsage: REN <old> <new>\n\n"); return; }
    int r = fat12_cwd_rename(a1, a2);
    if      (r == -1) terminal_write("\nFile not found.\n\n");
    else if (r == -2) terminal_write("\nTarget already exists.\n\n");
    else              terminal_write("\nFile renamed.\n\n");
}

/* ---------- program / batch execution ---------- */

static int run_batch(const char* filename);

/* ---------- history ---------- */

static void history_push(const char* line) {
    if (!line || !line[0]) return;

    if (g_hist_count > 0) {
        int last = (g_hist_head - 1 + HIST_MAX) % HIST_MAX;
        if (str_eq_ci(g_history[last], line)) return;
    }

    int i = 0;
    while (line[i] && i < HIST_LINE_MAX - 1) {
        g_history[g_hist_head][i] = line[i];
        i++;
    }
    g_history[g_hist_head][i] = '\0';
    g_hist_head = (g_hist_head + 1) % HIST_MAX;
    if (g_hist_count < HIST_MAX) g_hist_count++;
}

/* idx 0 = newest, 1 = one older, ... */
static const char* history_get(int idx) {
    if (idx < 0 || idx >= g_hist_count) return 0;
    int slot = (g_hist_head - 1 - idx + HIST_MAX * 2) % HIST_MAX;
    return g_history[slot];
}

/* ---------- line redraw ---------- */

static void redraw_input_line(int prompt_x, int prompt_y) {
    terminal_set_cursor(prompt_x, prompt_y);

    for (int i = 0; i < command_length; i++)
        terminal_putchar(command_buffer[i]);

    /* Blank the tail, but stop at column 78 so terminal_putchar never
     * wraps to the next row (which would trigger a scroll and shift
     * the visible prompt). */
    int col = prompt_x + command_length;
    while (col < 79) {
        terminal_putchar(' ');
        col++;
    }

    terminal_set_cursor(prompt_x + command_cursor, prompt_y);
}

static void cmd_find(char* args) {
    if (!args || args[0] == '\0') {
        terminal_write("\nUsage: FIND <filename>\n\n");
        return;
    }
    int hits = fat12_find_recursive(args);
    if (hits == 0) {
        terminal_write("\nNot found: ");
        terminal_write(args);
        terminal_write("\n\n");
    } else {
        terminal_write("\n");
        /* small int print */
        char b[8]; int n = 0; int v = hits;
        if (v == 0) b[n++] = '0';
        else { char t[8]; int tt = 0;
               while (v > 0) { t[tt++] = '0' + (v % 10); v /= 10; }
               while (tt > 0) b[n++] = t[--tt]; }
        b[n] = '\0';
        terminal_write(b);
        terminal_write(" match(es).\n\n");
    }
}

static void try_run_program(const char* name, int argc, char** argv) {
    char fname[16];
    int i = 0;
    while (name[i] && i < 8) { fname[i] = name[i]; i++; }
    fname[i++] = '.'; fname[i++] = 'C'; fname[i++] = 'O'; fname[i++] = 'M';
    fname[i] = '\0';

    if (fat12_cwd_exists(fname)) {
        int r = program_run_args(name, argc, argv);
        if (r < 0) terminal_write("\nFailed to load program.\n\n");
        return;
    }

    /* Fallback: \PROGRAMS\NAME.COM */
    {
        char pname[32];
        int k = 0;
        const char* pre = "\\PROGRAMS\\";
        while (pre[k] && k < 30) { pname[k] = pre[k]; k++; }
        int m = 0;
        while (fname[m] && k < 31) pname[k++] = fname[m++];
        pname[k] = '\0';
        if (fat12_cwd_exists(pname)) {
            int r = program_run_args(name, argc, argv);
            if (r < 0) terminal_write("\nFailed to load program.\n\n");
            return;
        }
    }

    /* Fall back to .BAT */
    i = 0;
    while (name[i] && i < 8) { fname[i] = name[i]; i++; }
    fname[i++] = '.'; fname[i++] = 'B'; fname[i++] = 'A'; fname[i++] = 'T';
    fname[i] = '\0';

    if (fat12_cwd_exists(fname)) {
        run_batch(fname);
        return;
    }

    terminal_write("\nUnknown command: ");
    terminal_write(name);
    terminal_write("\n\n");
}

static int run_batch(const char* filename) {
    if (g_batch_depth >= MAX_BATCH_DEPTH) {
        terminal_write("\nBatch nesting too deep.\n\n");
        return -1;
    }

    u32 size = fat12_cwd_size(filename);
    if (size == 0) return -1;

    terminal_write("[batch] size=");
    put_dec_dbg((int)size);
    terminal_write("\n");

    u8* buf = (u8*)kmalloc(size + 1);
    if (!buf) {
        terminal_write("[batch] kmalloc failed\n");
        return -1;
    }

    int n = fat12_cwd_read(filename, buf, size);
    terminal_write("[batch] read returned ");
    put_dec_dbg(n);
    terminal_write("\n");

    if (n <= 0) { kfree(buf); return -1; }
    buf[n] = '\0';

    g_batch_depth++;
    int saved_echo = g_batch_echo;

    int start = 0;
    for (int i = 0; i <= n; i++) {
        if (i == n || buf[i] == '\n' || buf[i] == '\r') {
            if (i > start) {
                char line[192];
                int len = i - start;
                if (len > 191) len = 191;
                for (int j = 0; j < len; j++) line[j] = (char)buf[start + j];
                line[len] = '\0';

                /* Trim trailing CR and whitespace */
                while (len > 0 &&
                       (line[len-1] == '\r' || line[len-1] == ' ' || line[len-1] == '\t'))
                    line[--len] = '\0';

                if (len > 0) {
                    /* Skip leading whitespace */
                    char* p = line;
                    while (*p == ' ' || *p == '\t') p++;

                    /* Handle REM */
                    int is_rem = 0;
                    if ((p[0] == 'r' || p[0] == 'R') &&
                        (p[1] == 'e' || p[1] == 'E') &&
                        (p[2] == 'm' || p[2] == 'M') &&
                        (p[3] == ' ' || p[3] == '\0')) is_rem = 1;

                    if (!is_rem) {
                        /* Detect @ECHO OFF / @ECHO ON */
                        const char* body = p;
                        int silent = 0;
                        if (*body == '@') { silent = 1; body++; }

                        int handled = 0;
                        if ((body[0] == 'e' || body[0] == 'E') &&
                            (body[1] == 'c' || body[1] == 'C') &&
                            (body[2] == 'h' || body[2] == 'H') &&
                            (body[3] == 'o' || body[3] == 'O')) {
                            char* q = (char*)body + 4;
                            while (*q == ' ') q++;
                            if ((q[0] == 'o' || q[0] == 'O') &&
                                (q[1] == 'f' || q[1] == 'F') &&
                                (q[2] == 'f' || q[2] == 'F') &&
                                (q[3] == '\0')) {
                                g_batch_echo = 0;
                                handled = 1;
                            } else if ((q[0] == 'o' || q[0] == 'O') &&
                                       (q[1] == 'n' || q[1] == 'N') &&
                                       (q[2] == '\0')) {
                                g_batch_echo = 1;
                                handled = 1;
                            }
                        }

                        if (!handled) {
                            if (g_batch_echo && !silent) {
                                terminal_write("> ");
                                terminal_write(body);
                                terminal_write("\n");
                            }
                            /* Dispatch as if typed */
                            extern void shell_execute_line(const char* line);
                            shell_execute_line(body);
                        }
                    }
                }
            }
            if (i < n && buf[i] == '\r' && i + 1 < n && buf[i+1] == '\n') i++;
            start = i + 1;
        }
    }

    g_batch_echo = saved_echo;
    g_batch_depth--;
    kfree(buf);
    return 0;
}

static void cmd_bat(char* args) {
    if (!args || args[0] == '\0') {
        terminal_write("\nUsage: BAT <file>\n\n");
        return;
    }
    if (fat12_cwd_exists(args)) { run_batch(args); return; }

    /* Append .BAT */
    char fname[32];
    int i = 0;
    while (args[i] && i < 27) { fname[i] = args[i]; i++; }
    fname[i++] = '.'; fname[i++] = 'B'; fname[i++] = 'A'; fname[i++] = 'T';
    fname[i] = '\0';

    if (fat12_cwd_exists(fname)) { run_batch(fname); return; }

    terminal_write("\nBatch file not found: ");
    terminal_write(args);
    terminal_write("\n\n");
}

/* ---------- env / settings ---------- */

static void cmd_env(char* args) {
    if (!args || args[0] == '\0') { env_list(); return; }

    /* Strip leading whitespace */
    while (*args == ' ') args++;

    if (*args == '-') {
        const char* name = args + 1;
        while (*name == ' ') name++;
        if (env_unset(name) < 0) {
            terminal_write("\nNot set: ");
            terminal_write(name);
            terminal_write("\n\n");
        } else {
            terminal_write("\nRemoved.\n\n");
        }
        return;
    }

    /* ENV NAME=VALUE  or  ENV NAME */
    char* eq = args;
    while (*eq && *eq != '=') eq++;

    if (*eq == '=') {
        *eq = '\0';
        char* name = args;
        const char* value = eq + 1;
        while (*name == ' ') name++;
        /* trim trailing space from name */
        int nl = 0;
        while (name[nl]) nl++;
        while (nl > 0 && name[nl-1] == ' ') name[--nl] = '\0';

        if (env_set(name, value) < 0) {
            terminal_write("\nCould not set variable.\n\n");
        } else {
            terminal_write("\n");
            terminal_write(name);
            terminal_write("=");
            terminal_write(value);
            terminal_write("\n\n");
        }
        return;
    }

    /* Show one */
    const char* name = args;
    const char* val = env_get(name);
    if (val) {
        terminal_write("\n");
        terminal_write(name);
        terminal_write(" = ");
        terminal_write(val);
        terminal_write("\n\n");
    } else {
        terminal_write("\nNot set: ");
        terminal_write(name);
        terminal_write("\n\n");
    }
}

static void cmd_set(char* args) {
    if (!args || args[0] == '\0') { settings_print(); return; }

    char* k = args;
    char* v = 0;
    while (*k == ' ') k++;
    if (*k == '\0') { settings_print(); return; }
    char* p = k;
    while (*p && *p != ' ') p++;
    if (*p == ' ') { *p = '\0'; v = p + 1; while (*v == ' ') v++; }
    if (v && *v == '\0') v = 0;

    if (!v) {
        if (str_eq_ci(k, "HOSTNAME")) {
            terminal_write("\nHOSTNAME = ");
            terminal_write(settings_get()->hostname);
            terminal_write("\n\n");
        } else if (str_eq_ci(k, "COLOR")) {
            terminal_write("\nCOLOR = ");
            char b[4]; int n = 0;
            unsigned char c = settings_get()->text_color;
            if (c >= 10) b[n++] = '1';
            b[n++] = '0' + (c % 10);
            b[n] = '\0';
            terminal_write(b);
            terminal_write("\n\n");
        } else if (str_eq_ci(k, "RUN")) {
            terminal_write("\nRun list:\n");
            settings_t* s = settings_get();
            if (s->startup_count == 0) terminal_write("  (none)\n");
            for (int i = 0; i < s->startup_count; i++) {
                terminal_write("  ");
                terminal_write(s->startup_progs[i]);
                terminal_write("\n");
            }
            terminal_write("\n");
        } else {
            terminal_write("\nUnknown setting: ");
            terminal_write(k);
            terminal_write("\n\n");
        }
        return;
    }

    int r = settings_set(k, v);
    if (r < 0) {
        terminal_write("\nCould not set ");
        terminal_write(k);
        terminal_write("\n\n");
        return;
    }
    settings_save();

    if (str_eq_ci(k, "COLOR")) terminal_set_color(settings_get()->text_color);

    if (str_eq_ci(k, "RUN")) {
        char upname[16];
        int j = 0;
        for (int i = 0; v[i] && j < 15; i++) {
            char c = v[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            upname[j++] = c;
        }
        upname[j] = '\0';
        task_t* t = task_create_program(upname, v);
        if (t) {
            terminal_write("\nSaved. Started ");
            terminal_write(upname);
            terminal_write(".\n\n");
        } else {
            terminal_write("\nSaved (will start next boot).\n\n");
        }
        return;
    }
    terminal_write("\nSaved.\n\n");
}

static void cmd_unset(char* args) {
    if (!args || args[0] == '\0') {
        terminal_write("\nUsage: UNSET RUN <prog>\n\n");
        return;
    }
    char* k = args;
    char* v = 0;
    while (*k == ' ') k++;
    char* p = k;
    while (*p && *p != ' ') p++;
    if (*p == ' ') { *p = '\0'; v = p + 1; while (*v == ' ') v++; }
    if (!v || *v == '\0') { terminal_write("\nUsage: UNSET RUN <prog>\n\n"); return; }

    int r = settings_unset(k, v);
    if (r < 0) {
        terminal_write("\nNot in startup list: ");
        terminal_write(v);
        terminal_write("\n\n");
        return;
    }
    settings_save();
    terminal_write("\nRemoved from startup.\n\n");
}

/* ---------- dispatch ---------- */

static void execute_line_internal(const char* input) {
    char expanded[MAX_COMMAND];
    env_expand(input, expanded, MAX_COMMAND);

    char scratch[MAX_COMMAND];
    int n = 0;
    while (expanded[n] && n < MAX_COMMAND - 1) { scratch[n] = expanded[n]; n++; }
    scratch[n] = '\0';

    char* cmd = scratch;
    char* rest = 0;
    for (int i = 0; i < n; i++) {
        if (cmd[i] == ' ') { cmd[i] = '\0'; rest = &cmd[i + 1]; break; }
    }

    to_upper(cmd);
    if (cmd[0] == '\0') return;

    if (str_eq_ci(cmd, "HELP"))   { cmd_help();   return; }
    if (str_eq_ci(cmd, "CLS"))    { print_banner(); return; }
    if (str_eq_ci(cmd, "VER"))    { cmd_ver();    return; }
    if (str_eq_ci(cmd, "ABOUT"))  { cmd_about();  return; }
    if (str_eq_ci(cmd, "TIME"))   { cmd_time();   return; }
    if (str_eq_ci(cmd, "TICKS"))  { cmd_ticks();  return; }
    if (str_eq_ci(cmd, "REBOOT")) { cmd_reboot(); return; }
    if (str_eq_ci(cmd, "SHUTDOWN") || str_eq_ci(cmd, "SHUT")) { cmd_shutdown(); return; }
    if (str_eq_ci(cmd, "MEM"))    { memory_print_stats(); return; }
    if (str_eq_ci(cmd, "MEMF"))   { memory_dump_freelist(); return; }
    if (str_eq_ci(cmd, "TASKS"))  { task_list_print(); return; }
    if (str_eq_ci(cmd, "CONFIG")) { settings_print_raw(); return; }
    if (str_eq_ci(cmd, "ENV"))    { cmd_env(rest); return; }
    if (str_eq_ci(cmd, "SET"))    { cmd_set(rest); return; }
    if (str_eq_ci(cmd, "UNSET"))  { cmd_unset(rest); return; }
    if (str_eq_ci(cmd, "DIR")) {
        char* a1; char* a2;
        split_args(rest ? rest : (char*)"", &a1, &a2);
        cmd_dir(a1);
        return;
    }
    if (str_eq_ci(cmd, "MD") || str_eq_ci(cmd, "MKDIR")) {
        char* a1; char* a2;
        split_args(rest ? rest : (char*)"", &a1, &a2);
        cmd_md(a1);
        return;
    }
    if (str_eq_ci(cmd, "CD") || str_eq_ci(cmd, "CHDIR")) {
        char* a1; char* a2;
        split_args(rest ? rest : (char*)"", &a1, &a2);
        cmd_cd(a1);
        return;
    }
    if (str_eq_ci(cmd, "RD") || str_eq_ci(cmd, "RMDIR")) {
        char* a1; char* a2;
        split_args(rest ? rest : (char*)"", &a1, &a2);
        cmd_rd(a1);
        return;
    }
    if (str_eq_ci(cmd, "TYPE")) {
        char* a1; char* a2;
        split_args(rest ? rest : (char*)"", &a1, &a2);
        cmd_type(a1);
        return;
    }
    if (str_eq_ci(cmd, "COPY")) {
        char* a1; char* a2;
        split_args(rest ? rest : (char*)"", &a1, &a2);
        cmd_copy(a1, a2);
        return;
    }
    if (str_eq_ci(cmd, "DEL")) {
        char* a1; char* a2;
        split_args(rest ? rest : (char*)"", &a1, &a2);
        cmd_del(a1);
        return;
    }
    if (str_eq_ci(cmd, "REN")) {
        char* a1; char* a2;
        split_args(rest ? rest : (char*)"", &a1, &a2);
        cmd_ren(a1, a2);
        return;
    }
    if (str_eq_ci(cmd, "FIND")) {
        char* a1; char* a2;
        split_args(rest ? rest : (char*)"", &a1, &a2);
        cmd_find(a1);
        return;
    }
    if (str_eq_ci(cmd, "BAT")) {
        char* a1; char* a2;
        split_args(rest ? rest : (char*)"", &a1, &a2);
        cmd_bat(a1);
        return;
    }
    if (str_eq_ci(cmd, "ECHO")) {
        terminal_write("\n");
        if (rest) terminal_write(rest);
        terminal_write("\n\n");
        return;
    }
    if (str_eq_ci(cmd, "SLEEP")) {
        char* a1; char* a2;
        split_args(rest ? rest : (char*)"", &a1, &a2);
        if (!a1) { terminal_write("\nUsage: SLEEP <ms>\n\n"); return; }
        unsigned int ms = 0;
        for (int i = 0; a1[i] >= '0' && a1[i] <= '9'; i++)
            ms = ms * 10 + (unsigned int)(a1[i] - '0');
        unsigned int target = timer_ticks() + (ms / 10);
        while (timer_ticks() < target) { __asm__ volatile ("hlt"); }
        return;
    }
    if (str_eq_ci(cmd, "BG")) {
        char* a1; char* a2;
        split_args(rest ? rest : (char*)"", &a1, &a2);
        if (!a1) { terminal_write("\nUsage: BG <prog>\n\n"); return; }
        char upname[16];
        int j = 0;
        for (int i = 0; a1[i] && j < 15; i++) {
            char c = a1[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            upname[j++] = c;
        }
        upname[j] = '\0';
        task_t* t = task_create_program(upname, a1);
        if (!t) { terminal_write("\nFailed to start background program.\n\n"); return; }
        terminal_write("\nStarted ");
        terminal_write(a1);
        terminal_write(".\n\n");
        return;
    }
    if (str_eq_ci(cmd, "KILL")) {
        char* a1; char* a2;
        split_args(rest ? rest : (char*)"", &a1, &a2);
        if (!a1) { terminal_write("\nUsage: KILL <pid>\n\n"); return; }
        unsigned int pid = 0;
        for (int i = 0; a1[i] >= '0' && a1[i] <= '9'; i++)
            pid = pid * 10 + (unsigned int)(a1[i] - '0');
        if (pid == 0 || pid == 1) {
            terminal_write("\nInvalid pid.\n\n");
            return;
        }
        task_kill(pid);
        terminal_write("\nTask marked for termination.\n\n");
        return;
    }
    if (str_eq_ci(cmd, "RUN")) {
        char* a1; char* a2;
        split_args(rest ? rest : (char*)"", &a1, &a2);
        if (!a1) { terminal_write("\nUsage: RUN <program>\n\n"); return; }
        char* run_argv[16];
        int run_argc = 0;
        run_argv[run_argc++] = a1;
        if (a2) run_argv[run_argc++] = a2;
        try_run_program(a1, run_argc, run_argv);
        return;
    }

    {
        char* gen_argv[16];
        int gen_argc = 0;
        gen_argv[gen_argc++] = cmd;
        if (rest) {
            char* p = rest;
            while (*p && gen_argc < 16) {
                while (*p == ' ') p++;
                if (*p == '\0') break;
                gen_argv[gen_argc++] = p;
                while (*p && *p != ' ') p++;
                if (*p) { *p = '\0'; p++; }
            }
        }
        try_run_program(cmd, gen_argc, gen_argv);
    }
}

/* Public entry point used by batch files */
void shell_execute_line(const char* line) {
    execute_line_internal(line);
}

static void execute_command(void) {
    command_buffer[command_length] = '\0';
    if (command_length > 0 && g_batch_depth == 0) {
        history_push(command_buffer);
    }
    execute_line_internal(command_buffer);
}

/* ---------- input ---------- */

static void read_line(void) {
    int prompt_x = 0, prompt_y = 0;
    terminal_get_cursor(&prompt_x, &prompt_y);

    command_length = 0;
    command_cursor = 0;
    g_hist_view    = -1;

    while (1) {
        char c = keyboard_getchar();

        /* Physical scroll keys */
        if (c == KEY_UP)   { terminal_scroll_up();   continue; }
        if (c == KEY_DOWN) { terminal_scroll_down(); continue; }

        /* Any other key: snap back to live view first */
        if (terminal_scroll_view() != 0) {
            terminal_scroll_reset();
            terminal_set_cursor(prompt_x + command_cursor, prompt_y);
        }

        if (c == '\n') {
            terminal_putchar('\n');
            return;
        }

        if (c == '\b') {                      /* Backspace */
            if (command_cursor > 0) {
                for (int i = command_cursor; i < command_length; i++)
                    command_buffer[i-1] = command_buffer[i];
                command_length--;
                command_cursor--;
                redraw_input_line(prompt_x, prompt_y);
            }
            continue;
        }

        if (c == KEY_DEL) {                    /* Delete */
            if (command_cursor < command_length) {
                for (int i = command_cursor + 1; i < command_length; i++)
                    command_buffer[i-1] = command_buffer[i];
                command_length--;
                redraw_input_line(prompt_x, prompt_y);
            }
            continue;
        }

        if (c == KEY_LEFT) {
            if (command_cursor > 0) {
                command_cursor--;
                terminal_set_cursor(prompt_x + command_cursor, prompt_y);
            }
            continue;
        }
        if (c == KEY_RIGHT) {
            if (command_cursor < command_length) {
                command_cursor++;
                terminal_set_cursor(prompt_x + command_cursor, prompt_y);
            }
            continue;
        }
        if (c == KEY_HOME) {
            command_cursor = 0;
            terminal_set_cursor(prompt_x, prompt_y);
            continue;
        }
        if (c == KEY_END) {
            command_cursor = command_length;
            terminal_set_cursor(prompt_x + command_cursor, prompt_y);
            continue;
        }

        if (c == KEY_CTRL_UP) {
            if (g_hist_count == 0) continue;
            if (g_hist_view < g_hist_count - 1) g_hist_view++;
            const char* h = history_get(g_hist_view);
            if (!h) continue;
            int i = 0;
            while (h[i] && i < MAX_COMMAND - 1) { command_buffer[i] = h[i]; i++; }
            command_buffer[i] = '\0';
            command_length = i;
            command_cursor = i;
            redraw_input_line(prompt_x, prompt_y);
            continue;
        }

        if (c == KEY_CTRL_DOWN) {
            if (g_hist_view < 0) continue;
            if (g_hist_view > 0) {
                g_hist_view--;
                const char* h = history_get(g_hist_view);
                if (!h) continue;
                int i = 0;
                while (h[i] && i < MAX_COMMAND - 1) { command_buffer[i] = h[i]; i++; }
                command_buffer[i] = '\0';
                command_length = i;
                command_cursor = i;
            } else {
                g_hist_view = -1;
                command_length = 0;
                command_cursor = 0;
            }
            redraw_input_line(prompt_x, prompt_y);
            continue;
        }

        if (c < 32 || c > 126) continue;

        if (command_length < MAX_COMMAND - 1) {
            /* Insert at cursor */
            for (int i = command_length; i > command_cursor; i--)
                command_buffer[i] = command_buffer[i-1];
            command_buffer[command_cursor] = c;
            command_length++;
            command_cursor++;
            redraw_input_line(prompt_x, prompt_y);
        }
    }
}

void shell_run(void) {
    env_init();
    refresh_cwd_display();
    print_banner();

    terminal_write("[boot] checking \\AUTOEXEC.BAT\n");
    int ax = fat12_cwd_exists("\\AUTOEXEC.BAT");
    terminal_write("[boot] exists=");
    terminal_putchar('0' + (ax ? 1 : 0));
    terminal_write("\n");

    /* Run AUTOEXEC.BAT if present at root */
    if (ax) {
        terminal_write("[boot] running...\n");
        int rb = run_batch("\\AUTOEXEC.BAT");
        terminal_write("[boot] run_batch returned ");
        if (rb < 0) terminal_putchar('-');
        int v = rb < 0 ? -rb : rb;
        terminal_putchar('0' + (v % 10));
        terminal_write("\n");
    } else {
        terminal_write("[boot] not found at root\n");
    }

    while (1) {
        terminal_write_color("C:", VGA_COLOR_LIGHT_GREY);
        terminal_write_color(cwd_display, VGA_COLOR_LIGHT_GREY);
        terminal_write_color(">", VGA_COLOR_LIGHT_GREY);
        read_line();
        execute_command();
    }
}
