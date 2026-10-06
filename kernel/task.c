#include "task.h"
#include "memory.h"
#include "screen.h"
#include "program.h"

#define STACK_SIZE 16384

static task_t* g_current = 0;
static task_t* g_tasks   = 0;
static unsigned int g_next_pid = 1;

/* The trampoline that new background tasks start in. */
static void task_program_trampoline(void);

void tasking_init(void) {
    task_t* t = (task_t*)kmalloc(sizeof(task_t));
    if (!t) return;

    t->esp = 0;
    t->pid = g_next_pid++;
    t->state = TASK_RUNNING;
    t->name[0] = 'S'; t->name[1] = 'H';
    t->name[2] = 'L'; t->name[3] = '\0';
    t->next = 0;
    t->stack_base = 0;
    t->stack_size = 0;
    t->counter = 0;
    t->prog_slot = -1;
    t->syscalls = *program_template();

    g_tasks   = t;
    g_current = t;
}

task_t* task_current(void) { return g_current; }
void    task_set_current(task_t* t) { g_current = t; }

static task_t* task_alloc(const char* name, void (*entry)(void)) {
    task_t* t = (task_t*)kmalloc(sizeof(task_t));
    if (!t) return 0;

    unsigned char* stack = (unsigned char*)kmalloc(STACK_SIZE);
    if (!stack) { kfree(t); return 0; }

    t->pid = g_next_pid++;
    t->state = TASK_READY;
    int i = 0;
    while (name[i] && i < 15) { t->name[i] = name[i]; i++; }
    t->name[i] = '\0';
    t->stack_base = (unsigned int)stack;
    t->stack_size = STACK_SIZE;
    t->counter = 0;
    t->prog_slot = -1;
    t->syscalls = *program_template();

    unsigned int top = (unsigned int)stack + STACK_SIZE;
    unsigned int* sp = (unsigned int*)top;

    *--sp = 0x202;
    *--sp = 0x08;
    *--sp = (unsigned int)entry;
    for (int k = 0; k < 8; k++) *--sp = 0;

    t->esp = (unsigned int)sp;

    t->next = g_tasks;
    g_tasks = t;
    return t;
}

task_t* task_create(const char* name, void (*entry)(void)) {
    return task_alloc(name, entry);
}

task_t* task_create_program(const char* name, const char* prog_name) {
    int slot = program_alloc_slot();
    if (slot < 0) return 0;

    int n = program_load_into_slot(prog_name, slot);
    if (n <= 0) { program_free_slot(slot); return 0; }

    task_t* t = task_alloc(name, task_program_trampoline);
    if (!t) { program_free_slot(slot); return 0; }

    t->prog_slot = slot;
    return t;
}

static void task_program_trampoline(void) {
    task_t* me = task_current();
    if (!me) return;

    if (me->prog_slot >= 0) {
        void* addr = program_slot_addr(me->prog_slot);
        if (addr) {
            typedef void (*entry_t)(syscalls_t*);
            entry_t e = (entry_t)addr;
            e(&me->syscalls);
        }
    }

    /* Program returned: mark dead.  Scheduler cleans up later. */
    me->state = TASK_DEAD;
    while (1) {
        __asm__ volatile ("hlt");
    }
}

task_t* task_pick_next(void) {
    if (!g_tasks) return 0;
    if (!g_current) return g_tasks;

    task_t* start = g_current->next ? g_current->next : g_tasks;
    task_t* t = start;
    do {
        if (t->state == TASK_READY) return t;
        t = t->next ? t->next : g_tasks;
    } while (t != start);

    return 0;
}

void task_kill(u32 pid) {
    task_t* t = g_tasks;
    while (t) {
        if (t->pid == pid && t->state != TASK_DEAD) {
            t->state = TASK_DEAD;
            return;
        }
        t = t->next;
    }
}

/* Called from task_pick_next path, before scheduling.  Unlinks and frees
 * any DEAD task that isn't the current one. */
void task_reap(void) {
    task_t* prev = 0;
    task_t* t = g_tasks;
    while (t) {
        if (t->state == TASK_DEAD && t != g_current) {
            task_t* dead = t;
            if (prev) prev->next = t->next;
            else      g_tasks = t->next;

            if (dead->prog_slot >= 0) program_free_slot(dead->prog_slot);
            if (dead->stack_base)     kfree((void*)dead->stack_base);
            kfree(dead);

            t = prev ? prev->next : g_tasks;
        } else {
            prev = t;
            t = t->next;
        }
    }
}

static void put_u32(unsigned int v) {
    char tmp[12]; int n = 0;
    if (v == 0) { terminal_putchar('0'); return; }
    while (v > 0) { tmp[n++] = '0' + (v % 10); v /= 10; }
    while (n > 0) terminal_putchar(tmp[--n]);
}

static void put_str_padded(const char* s, int width) {
    int n = 0;
    while (s[n] && n < width) { terminal_putchar(s[n]); n++; }
    for (; n < width; n++) terminal_putchar(' ');
}

void task_list_print(void) {
    terminal_write("\n");
    terminal_write_color("PID  NAME        STATE     CPU TICKS\n", VGA_COLOR_LIGHT_CYAN);
    terminal_write("\n");

    task_t* t = g_tasks;
    while (t) {
        put_u32(t->pid);
        terminal_write("    ");
        put_str_padded(t->name, 12);

        const char* st = "?";
        if (t->state == TASK_RUNNING) st = "RUNNING";
        else if (t->state == TASK_READY) st = "READY";
        else if (t->state == TASK_DEAD) st = "DEAD";
        put_str_padded(st, 10);

        put_u32(t->counter);
        terminal_write("\n");

        t = t->next;
    }
    terminal_write("\n");
}

int task_count(void) {
    int n = 0;
    task_t* t = g_tasks;
    while (t) { n++; t = t->next; }
    return n;
}
