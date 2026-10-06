#ifndef SETTINGS_H
#define SETTINGS_H

typedef struct {
    char hostname[24];
    unsigned char text_color;
    int  startup_count;
    char startup_progs[4][16];
} settings_t;

settings_t* settings_get(void);
void settings_init(void);
int  settings_load(void);
int  settings_save(void);
int  settings_set(const char* key, const char* value);
int  settings_unset(const char* key, const char* value);
void settings_print(void);
void settings_print_raw(void);
void settings_apply(void);

#endif
