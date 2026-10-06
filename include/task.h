#ifndef TASK_H
#define TASK_H

#include "program.h"

typedef unsigned int u32;

#define TASK_DEAD    0
#define TASK_READY   1
#define TASK_RUNNING 2

typedef struct task {
    u32           esp;
    u32           pid;
    int           state;
    char          name[16];
    struct task*  next;
    u32           stack_base;
    u32           stack_size;
    volatile u32  counter;
    int           prog_slot;      /* -1 if none */
    syscalls_t    syscalls;
} task_t;

void    tasking_init(void);
task_t* task_create(const char* name, void (*entry)(void));
task_t* task_create_program(const char* name, const char* prog_name);
task_t* task_current(void);
void    task_set_current(task_t* t);
task_t* task_pick_next(void);
void    task_kill(u32 pid);
void    task_list_print(void);
int     task_count(void);

#endif
