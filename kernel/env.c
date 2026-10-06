#include "env.h"
#include "screen.h"

typedef struct {
    char name[ENV_NAME_MAX];
    char value[ENV_VALUE_MAX];
} env_var_t;

static env_var_t g_env[ENV_MAX];
static int g_env_count = 0;

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

void env_init(void) {
    g_env_count = 0;
}

int env_set(const char* name, const char* value) {
    if (!name || !name[0]) return -1;

    for (int i = 0; i < g_env_count; i++) {
        if (streq_ci(g_env[i].name, name)) {
            int j = 0;
            while (value[j] && j < ENV_VALUE_MAX - 1) {
                g_env[i].value[j] = value[j];
                j++;
            }
            g_env[i].value[j] = '\0';
            return 0;
        }
    }
    if (g_env_count >= ENV_MAX) return -1;

    int j = 0;
    while (name[j] && j < ENV_NAME_MAX - 1) {
        char c = name[j];
        if (c >= 'a' && c <= 'z') c -= 32;
        g_env[g_env_count].name[j] = c;
        j++;
    }
    g_env[g_env_count].name[j] = '\0';

    j = 0;
    while (value[j] && j < ENV_VALUE_MAX - 1) {
        g_env[g_env_count].value[j] = value[j];
        j++;
    }
    g_env[g_env_count].value[j] = '\0';

    g_env_count++;
    return 0;
}

const char* env_get(const char* name) {
    for (int i = 0; i < g_env_count; i++) {
        if (streq_ci(g_env[i].name, name)) return g_env[i].value;
    }
    return 0;
}

int env_unset(const char* name) {
    for (int i = 0; i < g_env_count; i++) {
        if (streq_ci(g_env[i].name, name)) {
            for (int j = i; j < g_env_count - 1; j++) {
                for (int k = 0; k < ENV_NAME_MAX; k++)
                    g_env[j].name[k] = g_env[j+1].name[k];
                for (int k = 0; k < ENV_VALUE_MAX; k++)
                    g_env[j].value[k] = g_env[j+1].value[k];
            }
            g_env_count--;
            return 0;
        }
    }
    return -1;
}

void env_list(void) {
    terminal_write("\n");
    if (g_env_count == 0) {
        terminal_write("  (no environment variables)\n\n");
        return;
    }
    terminal_write_color("ENVIRONMENT\n\n", VGA_COLOR_LIGHT_CYAN);
    for (int i = 0; i < g_env_count; i++) {
        terminal_write("  ");
        terminal_write(g_env[i].name);
        terminal_write(" = ");
        terminal_write(g_env[i].value);
        terminal_write("\n");
    }
    terminal_write("\n");
}

int env_expand(const char* in, char* out, int max) {
    int oi = 0;
    int i = 0;
    while (in[i] && oi < max - 1) {
        if (in[i] == '%') {
            int j = i + 1;
            while (in[j] && in[j] != '%' && (j - i) < ENV_NAME_MAX) j++;
            if (in[j] == '%') {
                char name[ENV_NAME_MAX];
                int n = 0;
                for (int k = i + 1; k < j && n < ENV_NAME_MAX - 1; k++) {
                    name[n++] = in[k];
                }
                name[n] = '\0';
                const char* val = env_get(name);
                if (val) {
                    int vi = 0;
                    while (val[vi] && oi < max - 1) {
                        out[oi++] = val[vi++];
                    }
                }
                i = j + 1;
                continue;
            }
        }
        out[oi++] = in[i++];
    }
    out[oi] = '\0';
    return oi;
}
