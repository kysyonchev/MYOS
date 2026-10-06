#ifndef FAT12_H
#define FAT12_H

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

typedef struct {
    u8  name[8];
    u8  ext[3];
    u8  attr;
    u8  reserved;
    u8  create_tenths;
    u16 create_time;
    u16 create_date;
    u16 access_date;
    u16 high_cluster;
    u16 write_time;
    u16 write_date;
    u16 low_cluster;
    u32 size;
} __attribute__((packed)) fat_dir_entry_t;

int  fat12_init(void);

void fat12_cwd_print_entries(void);
void fat12_cwd_print_entries_at(const char* path);
int  fat12_cwd_read(const char* name, u8* buffer, u32 max);
u32  fat12_cwd_size(const char* name);
int  fat12_cwd_exists(const char* name);
int  fat12_cwd_write(const char* name, const u8* data, u32 size);
int  fat12_cwd_delete(const char* name);
int  fat12_cwd_rename(const char* oldname, const char* newname);
u16  fat12_cwd_cluster(void);

void fat12_cwd_path(char* out, int max);
int  fat12_cwd_mkdir(const char* name);
int  fat12_cwd_chdir(const char* name);
int  fat12_cwd_rmdir(const char* name);
int  fat12_find_recursive(const char* name);

#endif
