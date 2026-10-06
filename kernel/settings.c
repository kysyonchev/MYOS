#include "settings.h"
#include "fat12.h"
#include "screen.h"
#include "memory.h"
#include "task.h"

#define CFG_PATH "\\MYOS.CFG"
#define CFG_MAX  1024

static settings_t g_settings;

static int streq_ci(const char* a, const char* b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

static void str_copy(char* dst, const char* src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void str_copy_upper(char* dst, const char* src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        char c = src[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        dst[i] = c;
        i++;
    }
    dst[i] = '\0';
}

void settings_init(void) {
    str_copy(g_settings.hostname, "MYOS", sizeof(g_settings.hostname));
    g_settings.text_color = 0x07;
    g_settings.startup_count = 0;
    for (int i = 0; i < 4; i++) g_settings.startup_progs[i][0] = '\0';
}

settings_t* settings_get(void) { return &g_settings; }

static void parse_line(const char* line, int len) {
    int i = 0;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    if (i >= len) return;
    if (line[i] == '#' || line[i] == ';') return;

    int eq = -1;
    for (int j = i; j < len; j++) {
        if (line[j] == '=') { eq = j; break; }
    }
    if (eq < 0) return;

    int kend = eq;
    while (kend > i && (line[kend-1] == ' ' || line[kend-1] == '\t')) kend--;
    int kn = kend - i;

    int vstart = eq + 1;
    while (vstart < len && (line[vstart] == ' ' || line[vstart] == '\t')) vstart++;
    int vend = len;
    while (vend > vstart && (line[vend-1] == ' ' || line[vend-1] == '\t' ||
                             line[vend-1] == '\r' || line[vend-1] == '\n')) vend--;
    int vn = vend - vstart;

    char key[24];
    if (kn > 23) kn = 23;
    for (int j = 0; j < kn; j++) {
        char c = line[i + j];
        if (c >= 'a' && c <= 'z') c -= 32;
        key[j] = c;
    }
    key[kn] = '\0';

    if (streq_ci(key, "HOSTNAME")) {
        if (vn > 23) vn = 23;
        for (int j = 0; j < vn; j++) g_settings.hostname[j] = line[vstart + j];
        g_settings.hostname[vn] = '\0';
        return;
    }
    if (streq_ci(key, "COLOR")) {
        int base = 10, p = vstart, val = 0;
        if (p + 1 < vend && line[p] == '0' &&
            (line[p+1] == 'x' || line[p+1] == 'X')) { base = 16; p += 2; }
        for (; p < vend; p++) {
            char c = line[p];
            int d = -1;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else break;
            if (d >= base) break;
            val = val * base + d;
        }
        if (val >= 0 && val <= 15) g_settings.text_color = (unsigned char)val;
        return;
    }
    if (streq_ci(key, "RUN")) {
        if (g_settings.startup_count >= 4) return;
        if (vn == 0) return;
        if (vn > 15) vn = 15;
        int idx = g_settings.startup_count;
        for (int j = 0; j < vn; j++) {
            char c = line[vstart + j];
            if (c >= 'a' && c <= 'z') c -= 32;
            g_settings.startup_progs[idx][j] = c;
        }
        g_settings.startup_progs[idx][vn] = '\0';
        g_settings.startup_count++;
        return;
    }
}

int settings_load(void) {
    settings_init();

    u8* buf = (u8*)kmalloc(CFG_MAX);
    if (!buf) return -1;

    int n = fat12_cwd_read(CFG_PATH, buf, CFG_MAX);
    if (n <= 0) { kfree(buf); return 0; }

    int start = 0;
    for (int i = 0; i <= n; i++) {
        if (i == n || buf[i] == '\n' || buf[i] == '\r') {
            if (i > start) parse_line((const char*)(buf + start), i - start);
            if (i < n && buf[i] == '\r' && i + 1 < n && buf[i+1] == '\n') i++;
            start = i + 1;
        }
    }
    kfree(buf);
    return 0;
}

int settings_save(void) {
    char buf[768];
    int p = 0;
    #define APP(s) do { const char* _s = (s); while (*_s && p < 700) buf[p++] = *_s++; } while (0)
    #define APPC(c) do { if (p < 700) buf[p++] = (c); } while (0)

    APP("# MYOS.CFG - configuration file\n");
    APP("# Edited with SET, or with EDIT.COM\n\n");
    APP("HOSTNAME=");
    for (int i = 0; g_settings.hostname[i] && p < 700; i++)
        buf[p++] = g_settings.hostname[i];
    APPC('\n');
    APP("COLOR=");
    if (g_settings.text_color >= 10) APPC('1');
    APPC('0' + (g_settings.text_color % 10));
    APPC('\n');
    for (int i = 0; i < g_settings.startup_count; i++) {
        APP("RUN=");
        for (int j = 0; g_settings.startup_progs[i][j] && p < 700; j++)
            buf[p++] = g_settings.startup_progs[i][j];
        APPC('\n');
    }
    #undef APP
    #undef APPC

    if (fat12_cwd_exists(CFG_PATH)) fat12_cwd_delete(CFG_PATH);
    return fat12_cwd_write(CFG_PATH, (const u8*)buf, (u32)p);
}

int settings_set(const char* key, const char* value) {
    if (streq_ci(key, "HOSTNAME")) {
        str_copy(g_settings.hostname, value, sizeof(g_settings.hostname));
        return 0;
    }
    if (streq_ci(key, "COLOR")) {
        int val = 0;
        for (int i = 0; value[i]; i++) {
            char c = value[i];
            if (c < '0' || c > '9') return -1;
            val = val * 10 + (c - '0');
            if (val > 99) return -1;
        }
        if (val < 0 || val > 15) return -1;
        g_settings.text_color = (unsigned char)val;
        return 0;
    }
    if (streq_ci(key, "RUN")) {
        if (g_settings.startup_count >= 4) return -1;
        for (int i = 0; i < g_settings.startup_count; i++) {
            if (streq_ci(g_settings.startup_progs[i], value)) return 0;
        }
        int idx = g_settings.startup_count;
        str_copy_upper(g_settings.startup_progs[idx], value, 16);
        g_settings.startup_count++;
        return 0;
    }
    return -1;
}

int settings_unset(const char* key, const char* value) {
    if (streq_ci(key, "RUN")) {
        int found = -1;
        for (int i = 0; i < g_settings.startup_count; i++) {
            if (streq_ci(g_settings.startup_progs[i], value)) { found = i; break; }
        }
        if (found < 0) return -1;
        for (int i = found; i < g_settings.startup_count - 1; i++) {
            str_copy(g_settings.startup_progs[i],
                     g_settings.startup_progs[i + 1], 16);
        }
        g_settings.startup_count--;
        return 0;
    }
    return -1;
}

static void print_uint(unsigned int v) {
    char tmp[12]; int n = 0;
    if (v == 0) { terminal_putchar('0'); return; }
    while (v > 0) { tmp[n++] = '0' + (v % 10); v /= 10; }
    while (n > 0) terminal_putchar(tmp[--n]);
}

static void print_padded(const char* s, int width) {
    int n = 0;
    while (s[n] && n < width) { terminal_putchar(s[n]); n++; }
    for (; n < width; n++) terminal_putchar(' ');
}

void settings_print(void) {
    terminal_write("\n");
    terminal_write_color("MYOS SETTINGS\n\n", VGA_COLOR_LIGHT_CYAN);

    terminal_write("  ");
    print_padded("HOSTNAME", 10);
    terminal_write(" = ");
    terminal_write(g_settings.hostname);
    terminal_write("\n");

    terminal_write("  ");
    print_padded("COLOR", 10);
    terminal_write(" = ");
    print_uint(g_settings.text_color);
    terminal_write("\n");

    terminal_write("  ");
    print_padded("RUN", 10);
    terminal_write(" = ");
    if (g_settings.startup_count == 0) {
        terminal_write("(none)\n");
    } else {
        for (int i = 0; i < g_settings.startup_count; i++) {
            if (i > 0) {
                terminal_write("  ");
                print_padded("", 10);
                terminal_write("   ");
            }
            terminal_write(g_settings.startup_progs[i]);
            terminal_write("\n");
        }
    }
    terminal_write("\n");
    terminal_write("Use SET <key> <value> to change.\n");
    terminal_write("Use SET RUN <prog> to auto-start a program at boot.\n");
    terminal_write("Use UNSET RUN <prog> to remove one.\n\n");
}

void settings_print_raw(void) {
    u8* buf = (u8*)kmalloc(CFG_MAX);
    if (!buf) return;
    int n = fat12_cwd_read(CFG_PATH, buf, CFG_MAX);
    if (n <= 0) {
        terminal_write("\nC:\\MYOS.CFG does not exist.\n\n");
        kfree(buf);
        return;
    }
    terminal_write("\n--- C:\\MYOS.CFG ---\n");
    for (int i = 0; i < n; i++) terminal_putchar((char)buf[i]);
    terminal_write("--- end ---\n\n");
    kfree(buf);
}

void settings_apply(void) {
    terminal_set_color(g_settings.text_color);
    for (int i = 0; i < g_settings.startup_count; i++) {
        task_create_program(g_settings.startup_progs[i],
                            g_settings.startup_progs[i]);
    }
}
