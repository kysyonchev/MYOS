#ifndef MEMORY_H
#define MEMORY_H

void  memory_init(void);
void* kmalloc(unsigned int size);
void  kfree(void* ptr);
void  memory_print_stats(void);
void  memory_dump_freelist(void);

#endif
