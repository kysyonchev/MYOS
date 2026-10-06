#include "fat12.h"
#include "ata.h"
#include "screen.h"

#define SECTOR_SIZE 512
#define MAX_FAT_BYTES   (64 * SECTOR_SIZE)
#define DIR_MAX_ENTRIES 256
#define DIR_MAX_BYTES   (DIR_MAX_ENTRIES * 32)

typedef struct {
    u16 cluster;
    u32 num_entries;
    u8  entries[DIR_MAX_BYTES];
} dir_t;

static dir_t g_cwd;
static dir_t g_scratch_dir;

static u8  fat_buffer[MAX_FAT_BYTES];
static u8  sector_buffer[SECTOR_SIZE];

static u16 bytes_per_sector;
static u8  sectors_per_cluster;
static u16 reserved_sectors;
static u8  num_fats;
static u16 root_entries;
static u16 sectors_per_fat;
static u32 root_dir_start_sector;
static u32 data_start_sector;
static u32 root_dir_sectors;
static u16 fat_size_bytes;

/* ============ FAT access ============ */

static u16 fat12_get_entry(u16 cluster) {
    u32 off = cluster + (cluster / 2);
    if (off + 1 >= fat_size_bytes) return 0xFFF;
    u16 v = fat_buffer[off] | (fat_buffer[off + 1] << 8);
    if (cluster & 1) v >>= 4;
    else             v &= 0x0FFF;
    return v;
}

static void fat12_set_entry(u16 cluster, u16 value) {
    u32 off = cluster + (cluster / 2);
    if (off + 1 >= fat_size_bytes) return;
    if (cluster & 1) {
        fat_buffer[off]     = (fat_buffer[off] & 0x0F) | ((value << 4) & 0xF0);
        fat_buffer[off + 1] = (value >> 4) & 0xFF;
    } else {
        fat_buffer[off]     = value & 0xFF;
        fat_buffer[off + 1] = (fat_buffer[off + 1] & 0xF0) | ((value >> 8) & 0x0F);
    }
}

static void fat12_flush_fat(void) {
    for (u8 i = 0; i < num_fats; i++) {
        ata_write_sectors(reserved_sectors + i * sectors_per_fat,
                          (unsigned char)sectors_per_fat, fat_buffer);
    }
}

static u32 cluster_to_sector(u16 cluster) {
    return data_start_sector + (u32)(cluster - 2) * sectors_per_cluster;
}

static u16 fat12_alloc_cluster(void) {
    u32 total = fat_size_bytes * 2 / 3;
    if (total > 4000) total = 4000;
    for (u16 c = 2; c < total; c++) {
        if (fat12_get_entry(c) == 0x000) {
            fat12_set_entry(c, 0xFFF);
            return c;
        }
    }
    return 0;
}

static void fat12_free_chain(u16 start) {
    u16 c = start;
    int guard = 0;
    while (c >= 2 && c < 0xFF8 && guard < 4000) {
        u16 next = fat12_get_entry(c);
        fat12_set_entry(c, 0x000);
        c = next;
        guard++;
    }
}

/* ============ Name handling ============ */

static void split_name(const char* in, char out[12]) {
    for (int i = 0; i < 11; i++) out[i] = ' ';
    int i = 0, j = 0;
    while (in[i] && in[i] != '.' && j < 8) {
        char c = in[i++];
        if (c >= 'a' && c <= 'z') c -= 32;
        out[j++] = c;
    }
    while (in[i] && in[i] != '.') i++;
    if (in[i] == '.') {
        i++;
        j = 8;
        int k = 0;
        while (in[i] && k < 3) {
            char c = in[i++];
            if (c >= 'a' && c <= 'z') c -= 32;
            out[j++] = c;
            k++;
        }
    }
}

static int is_dot_entry(const fat_dir_entry_t* e) {
    if (e->name[0] != '.') return 0;
    if (e->name[1] == ' ') return 1;
    if (e->name[1] == '.') return 2;
    return 0;
}

static int entry_name_matches(const fat_dir_entry_t* e, const char* name) {
    if (name[0] == '.' && name[1] == '\0') return is_dot_entry(e) == 1;
    if (name[0] == '.' && name[1] == '.' && name[2] == '\0') return is_dot_entry(e) == 2;
    char want[12];
    split_name(name, want);
    for (int i = 0; i < 11; i++) {
        if ((char)e->name[i] != want[i]) return 0;
    }
    return 1;
}

/* ============ Directory read / write ============ */

static int dir_read(u16 cluster, dir_t* d) {
    d->cluster = cluster;
    u32 total = 0;

    if (cluster == 0) {
        for (u32 i = 0; i < root_dir_sectors; i++) {
            if (ata_read_sectors(root_dir_start_sector + i, 1,
                                 d->entries + i * 512) < 0) return -1;
            total += 512;
        }
        d->num_entries = root_entries;
        return 0;
    }

    u16 c = cluster;
    int guard = 0;
    while (c >= 2 && c < 0xFF8 && guard < 4000) {
        if (total + 512 > DIR_MAX_BYTES) break;
        if (ata_read_sectors(cluster_to_sector(c), 1,
                             d->entries + total) < 0) return -1;
        total += 512;
        c = fat12_get_entry(c);
        guard++;
    }
    d->num_entries = total / 32;
    return 0;
}

static int dir_write(dir_t* d) {
    if (d->cluster == 0) {
        for (u32 i = 0; i < root_dir_sectors; i++) {
            if (ata_write_sectors(root_dir_start_sector + i, 1,
                                  d->entries + i * 512) < 0) return -1;
        }
    } else {
        u16 c = d->cluster;
        u32 written = 0;
        int guard = 0;
        while (c >= 2 && c < 0xFF8 && written < d->num_entries * 32 && guard < 4000) {
            if (ata_write_sectors(cluster_to_sector(c), 1,
                                  d->entries + written) < 0) return -1;
            written += 512;
            c = fat12_get_entry(c);
            guard++;
        }
    }
    /* Keep the in-memory cwd in sync when we just wrote the cwd itself */
    if (d != &g_cwd && d->cluster == g_cwd.cluster) {
        g_cwd = *d;
    }
    return 0;
}

static fat_dir_entry_t* dir_find(dir_t* d, const char* name) {
    for (u32 i = 0; i < d->num_entries; i++) {
        fat_dir_entry_t* e = (fat_dir_entry_t*)(d->entries + i * 32);
        if (e->name[0] == 0x00) return 0;
        if (e->name[0] == 0xE5) continue;
        if (e->attr & 0x08) continue;
        if (e->attr == 0x0F) continue;
        if (entry_name_matches(e, name)) return e;
    }
    return 0;
}

static int dir_free_slot(dir_t* d) {
    for (u32 i = 0; i < d->num_entries; i++) {
        fat_dir_entry_t* e = (fat_dir_entry_t*)(d->entries + i * 32);
        if (e->name[0] == 0x00 || e->name[0] == 0xE5) return (int)i;
    }
    return -1;
}

/* ============ Path handling ============ */

/* Split "A\B\NAME" into dirpath="A\B" and lastname="NAME".
 * If there's no separator, dirpath is empty and lastname is the whole path. */
static int split_path(const char* path, char* dirpath, char* lastname, int dpmax) {
    int lastsep = -1;
    for (int i = 0; path[i]; i++) {
        if (path[i] == '\\' || path[i] == '/') lastsep = i;
    }
    if (lastsep < 0) {
        dirpath[0] = '\0';
        int i = 0;
        while (path[i] && i < 31) { lastname[i] = path[i]; i++; }
        lastname[i] = '\0';
        return 0;
    }
    int dpi = 0;
    int i;
    if (path[0] == '\\' || path[0] == '/') {
        if (dpi < dpmax - 1) dirpath[dpi++] = '\\';
        for (i = 1; i < lastsep && dpi < dpmax - 1; i++) dirpath[dpi++] = path[i];
    } else {
        for (i = 0; i < lastsep && dpi < dpmax - 1; i++) dirpath[dpi++] = path[i];
    }
    dirpath[dpi] = '\0';
    int j = 0;
    for (i = lastsep + 1; path[i] && j < 31; i++, j++) lastname[j] = path[i];
    lastname[j] = '\0';
    return 1;
}

/* Resolve a directory path into a dir_t.
 * - ""              -> current directory
 * - "GAMES"         -> GAMES under current
 * - "\GAMES\SNAKE"  -> absolute from root
 * - ".." or "..\X"  -> walk up */
static int resolve_dir(const char* path, dir_t* out) {
    if (path[0] == '\0') { *out = g_cwd; return 0; }

    const char* p = path;
    if (*p == '\\' || *p == '/') {
        if (dir_read(0, out) < 0) return -1;
        p++;
    } else {
        *out = g_cwd;
    }

    while (*p) {
        char comp[16];
        int n = 0;
        while (*p && *p != '\\' && *p != '/') {
            if (n < 15) comp[n++] = *p;
            p++;
        }
        comp[n] = '\0';
        if (*p) p++;

        if (comp[0] == '\0') continue;
        if (comp[0] == '.' && comp[1] == '\0') continue;

        if (comp[0] == '.' && comp[1] == '.' && comp[2] == '\0') {
            if (out->cluster == 0) continue;
            fat_dir_entry_t* dotdot = dir_find(out, "..");
            if (!dotdot) return -2;
            if (dir_read(dotdot->low_cluster, out) < 0) return -3;
            continue;
        }

        fat_dir_entry_t* e = dir_find(out, comp);
        if (!e) return -4;
        if (!(e->attr & 0x10)) return -5;
        if (e->low_cluster < 2) return -6;
        if (dir_read(e->low_cluster, out) < 0) return -7;
    }
    return 0;
}

/* ============ Init ============ */

int fat12_init(void) {
    if (ata_read_sectors(0, 1, sector_buffer) < 0) return -1;

    bytes_per_sector    = sector_buffer[11] | (sector_buffer[12] << 8);
    sectors_per_cluster = sector_buffer[13];
    reserved_sectors    = sector_buffer[14] | (sector_buffer[15] << 8);
    num_fats            = sector_buffer[16];
    root_entries        = sector_buffer[17] | (sector_buffer[18] << 8);
    sectors_per_fat     = sector_buffer[22] | (sector_buffer[23] << 8);

    if (bytes_per_sector != 512) return -2;

    fat_size_bytes = sectors_per_fat * 512;
    if (fat_size_bytes > MAX_FAT_BYTES) return -4;

    root_dir_start_sector = reserved_sectors + num_fats * sectors_per_fat;
    root_dir_sectors      = (root_entries * 32 + 511) / 512;
    data_start_sector     = root_dir_start_sector + root_dir_sectors;

    ata_read_sectors(reserved_sectors, sectors_per_fat, fat_buffer);

    return dir_read(0, &g_cwd);
}

/* ============ Listing ============ */

static void print_padded_name(const fat_dir_entry_t* e) {
    char line[14];
    int p = 0;
    for (int j = 0; j < 8; j++) {
        char c = (char)e->name[j];
        if (c == ' ') break;
        line[p++] = c;
    }
    int ext_has = 0;
    for (int j = 0; j < 3; j++) {
        if (e->ext[j] != ' ') { ext_has = 1; break; }
    }
    if (ext_has) {
        line[p++] = '.';
        for (int j = 0; j < 3; j++) {
            char c = (char)e->ext[j];
            if (c == ' ') break;
            line[p++] = c;
        }
    }
    line[p] = '\0';
    terminal_write(line);
    for (int k = p; k < 12; k++) terminal_putchar(' ');
}

static void print_u32(u32 v) {
    char num[12];
    int n = 0;
    if (v == 0) num[n++] = '0';
    else {
        char tmp[12]; int t = 0;
        while (v > 0) { tmp[t++] = '0' + (v % 10); v /= 10; }
        while (t > 0) num[n++] = tmp[--t];
    }
    num[n] = '\0';
    terminal_write(num);
}

static void print_entries_in(dir_t* d) {
    int count = 0;
    for (u32 i = 0; i < d->num_entries; i++) {
        fat_dir_entry_t* e = (fat_dir_entry_t*)(d->entries + i * 32);
        if (e->name[0] == 0x00) break;
        if (e->name[0] == 0xE5) continue;
        if (e->attr & 0x08) continue;
        if (e->attr == 0x0F) continue;

        print_padded_name(e);
        if (e->attr & 0x10) {
            terminal_write("<DIR>");
        } else {
            print_u32(e->size);
        }
        terminal_write("\n");
        count++;
    }

    terminal_write("\n        ");
    print_u32((u32)count);
    terminal_write(" File(s)\n\n");
}

void fat12_cwd_print_entries(void) {
    print_entries_in(&g_cwd);
}

void fat12_cwd_print_entries_at(const char* path) {
    if (path[0] == '\0') { print_entries_in(&g_cwd); return; }
    dir_t d;
    if (resolve_dir(path, &d) < 0) {
        terminal_write("\nDirectory not found: ");
        terminal_write(path);
        terminal_write("\n\n");
        return;
    }
    print_entries_in(&d);
}

/* ============ File read ============ */

static int read_file_chain(u16 start_cluster, u32 size, u8* buf, u32 max) {
    u32 written = 0;
    u16 cluster = start_cluster;
    while (cluster >= 2 && cluster < 0xFF8 && written < size) {
        u32 sector = cluster_to_sector(cluster);
        for (u8 s = 0; s < sectors_per_cluster && written < size; s++) {
            if (ata_read_sectors(sector + s, 1, sector_buffer) < 0) return -2;
            u32 chunk = size - written;
            if (chunk > 512) chunk = 512;
            if (written + chunk > max) chunk = max - written;
            for (u32 i = 0; i < chunk; i++) {
                buf[written + i] = sector_buffer[i];
            }
            written += chunk;
        }
        cluster = fat12_get_entry(cluster);
    }
    return (int)written;
}

int fat12_cwd_read(const char* path, u8* buf, u32 max) {
    char dpath[64], fname[32];
    split_path(path, dpath, fname, sizeof(dpath));
    if (fname[0] == '\0') return -1;

    dir_t d;
    if (resolve_dir(dpath, &d) < 0) return -1;

    fat_dir_entry_t* e = dir_find(&d, fname);
    if (!e) return -1;
    if (e->attr & 0x10) return -1;
    return read_file_chain(e->low_cluster, e->size, buf, max);
}

int fat12_cwd_exists(const char* path) {
    char dpath[64], fname[32];
    split_path(path, dpath, fname, sizeof(dpath));
    if (fname[0] == '\0') return 0;

    dir_t d;
    if (resolve_dir(dpath, &d) < 0) return 0;
    return dir_find(&d, fname) != 0;
}

u32 fat12_cwd_size(const char* path) {
    char dpath[64], fname[32];
    split_path(path, dpath, fname, sizeof(dpath));
    if (fname[0] == '\0') return 0;

    dir_t d;
    if (resolve_dir(dpath, &d) < 0) return 0;
    fat_dir_entry_t* e = dir_find(&d, fname);
    return e ? e->size : 0;
}

u16 fat12_cwd_cluster(void) {
    return g_cwd.cluster;
}

/* ============ File write ============ */

int fat12_cwd_write(const char* path, const u8* data, u32 size) {
    char dpath[64], fname[32];
    split_path(path, dpath, fname, sizeof(dpath));
    if (fname[0] == '\0') return -1;

    dir_t d;
    if (resolve_dir(dpath, &d) < 0) return -1;

    if (dir_find(&d, fname)) return -1;

    int slot = dir_free_slot(&d);
    if (slot < 0) return -2;

    u16 first_cluster = 0;

    if (size > 0) {
        u32 sectors_needed = (size + 511) / 512;
        u16 prev = 0;

        for (u32 i = 0; i < sectors_needed; i++) {
            u16 c = fat12_alloc_cluster();
            if (c == 0) {
                if (first_cluster) fat12_free_chain(first_cluster);
                return -3;
            }
            if (i == 0) first_cluster = c;
            if (prev) fat12_set_entry(prev, c);
            prev = c;
        }
        fat12_set_entry(prev, 0xFFF);

        u32 remaining = size;
        u32 offset = 0;
        u16 c = first_cluster;
        while (c >= 2 && c < 0xFF8 && remaining > 0) {
            u32 sector = cluster_to_sector(c);
            u16 next = fat12_get_entry(c);
            for (u8 s = 0; s < sectors_per_cluster && remaining > 0; s++) {
                for (int i = 0; i < 512; i++) sector_buffer[i] = 0;
                u32 chunk = remaining > 512 ? 512 : remaining;
                for (u32 i = 0; i < chunk; i++) {
                    sector_buffer[i] = data[offset + i];
                }
                if (ata_write_sectors(sector + s, 1, sector_buffer) < 0) return -4;
                offset += chunk;
                remaining -= chunk;
            }
            c = next;
        }
    }

    fat_dir_entry_t* entry = (fat_dir_entry_t*)(d.entries + slot * 32);
    u8* raw = (u8*)entry;
    for (int i = 0; i < 32; i++) raw[i] = 0;

    char want[12];
    split_name(fname, want);
    for (int i = 0; i < 11; i++) raw[i] = (u8)want[i];
    entry->attr = 0x20;
    entry->low_cluster = first_cluster;
    entry->size = size;

    fat12_flush_fat();
    dir_write(&d);
    return (int)size;
}

/* ============ Delete / rename ============ */

int fat12_cwd_delete(const char* path) {
    char dpath[64], fname[32];
    split_path(path, dpath, fname, sizeof(dpath));
    if (fname[0] == '\0') return -1;

    dir_t d;
    if (resolve_dir(dpath, &d) < 0) return -1;

    fat_dir_entry_t* e = dir_find(&d, fname);
    if (!e) return -1;
    if (e->attr & 0x10) return -1;

    if (e->low_cluster >= 2) {
        fat12_free_chain(e->low_cluster);
    }
    e->name[0] = 0xE5;

    fat12_flush_fat();
    dir_write(&d);
    return 0;
}

int fat12_cwd_rename(const char* oldpath, const char* newpath) {
    char olddp[64], oldfn[32];
    char newdp[64], newfn[32];
    split_path(oldpath, olddp, oldfn, sizeof(olddp));
    split_path(newpath, newdp, newfn, sizeof(newdp));

    if (oldfn[0] == '\0' || newfn[0] == '\0') return -1;

    dir_t od;
    if (resolve_dir(olddp, &od) < 0) return -1;

    dir_t nd;
    if (newdp[0] == '\0') {
        nd = od;
    } else {
        if (resolve_dir(newdp, &nd) < 0) return -1;
    }

    if (nd.cluster != od.cluster) return -1;

    if (dir_find(&od, newfn)) return -2;
    fat_dir_entry_t* e = dir_find(&od, oldfn);
    if (!e) return -1;

    char want[12];
    split_name(newfn, want);
    for (int i = 0; i < 11; i++) {
        ((u8*)e)[i] = (u8)want[i];
    }

    dir_write(&od);
    return 0;
}

/* ============ Display path (walk up from cwd to root) ============ */

void fat12_cwd_path(char* out, int max) {
    char components[8][12];
    int n = 0;

    dir_t cur = g_cwd;
    while (cur.cluster != 0 && n < 8) {
        fat_dir_entry_t* dotdot = dir_find(&cur, "..");
        if (!dotdot) break;

        dir_t parent;
        if (dir_read(dotdot->low_cluster, &parent) < 0) break;

        int found = 0;
        for (u32 i = 0; i < parent.num_entries; i++) {
            fat_dir_entry_t* e = (fat_dir_entry_t*)(parent.entries + i * 32);
            if (e->name[0] == 0x00) break;
            if (e->name[0] == 0xE5) continue;
            if (e->attr & 0x08) continue;
            if (e->attr == 0x0F) continue;
            if (!(e->attr & 0x10)) continue;
            if (is_dot_entry(e)) continue;
            if (e->low_cluster == cur.cluster) {
                int j = 0;
                for (int k = 0; k < 8 && e->name[k] != ' '; k++) {
                    components[n][j++] = e->name[k];
                }
                components[n][j] = '\0';
                n++;
                found = 1;
                break;
            }
        }
        if (!found) break;
        cur = parent;
    }

    int p = 0;
    if (p < max - 1) out[p++] = 92;
    for (int i = n - 1; i >= 0; i--) {
        if (i < n - 1 && p < max - 1) out[p++] = 92;
        for (int j = 0; components[i][j] && p < max - 1; j++) {
            out[p++] = components[i][j];
        }
    }
    out[p] = '\0';
}

/* ============ Directory commands ============ */

int fat12_cwd_mkdir(const char* path) {
    char dpath[64], newdir[32];
    split_path(path, dpath, newdir, sizeof(dpath));
    if (newdir[0] == '\0') return -10;

    dir_t d;
    if (resolve_dir(dpath, &d) < 0) return -11;   /* parent not found */

    if (dir_find(&d, newdir)) return -12;         /* already exists */

    int slot = dir_free_slot(&d);
    if (slot < 0) return -13;

    u16 new_cluster = fat12_alloc_cluster();
    if (new_cluster == 0) return -14;

    u8 dir_data[512];
    for (int i = 0; i < 512; i++) dir_data[i] = 0;

    fat_dir_entry_t* dot = (fat_dir_entry_t*)dir_data;
    for (int i = 0; i < 11; i++) dot->name[i] = ' ';
    dot->name[0] = '.';
    dot->attr = 0x10;
    dot->low_cluster = new_cluster;
    dot->size = 0;

    fat_dir_entry_t* dotdot = (fat_dir_entry_t*)(dir_data + 32);
    for (int i = 0; i < 11; i++) dotdot->name[i] = ' ';
    dotdot->name[0] = '.';
    dotdot->name[1] = '.';
    dotdot->attr = 0x10;
    dotdot->low_cluster = d.cluster;
    dotdot->size = 0;

    if (ata_write_sectors(cluster_to_sector(new_cluster), 1, dir_data) < 0) {
        fat12_free_chain(new_cluster);
        return -4;
    }

    fat_dir_entry_t* entry = (fat_dir_entry_t*)(d.entries + slot * 32);
    u8* raw = (u8*)entry;
    for (int i = 0; i < 32; i++) raw[i] = 0;

    char want[12];
    split_name(newdir, want);
    for (int i = 0; i < 11; i++) raw[i] = (u8)want[i];
    entry->attr = 0x10;
    entry->low_cluster = new_cluster;
    entry->size = 0;

    fat12_flush_fat();
    dir_write(&d);
    return 0;
}

int fat12_cwd_chdir(const char* path) {
    if (path[0] == '\0') return 0;

    dir_t target;
    if (resolve_dir(path, &target) < 0) return -1;
    g_cwd = target;
    return 0;
}

int fat12_cwd_rmdir(const char* path) {
    char dpath[64], name[32];
    split_path(path, dpath, name, sizeof(dpath));
    if (name[0] == '\0') return -1;

    dir_t d;
    if (resolve_dir(dpath, &d) < 0) return -1;

    fat_dir_entry_t* e = dir_find(&d, name);
    if (!e) return -1;
    if (!(e->attr & 0x10)) return -2;
    if (e->low_cluster < 2) return -3;

    if (dir_read(e->low_cluster, &g_scratch_dir) < 0) return -4;

    for (u32 i = 0; i < g_scratch_dir.num_entries; i++) {
        fat_dir_entry_t* s = (fat_dir_entry_t*)(g_scratch_dir.entries + i * 32);
        if (s->name[0] == 0x00) break;
        if (s->name[0] == 0xE5) continue;
        if (s->attr & 0x08) continue;
        if (s->attr == 0x0F) continue;
        if (is_dot_entry(s)) continue;
        return -5;
    }

    fat12_free_chain(e->low_cluster);
    e->name[0] = 0xE5;

    fat12_flush_fat();
    dir_write(&d);
    return 0;
}

/* ============ Recursive file search ============ */

#define FIND_MAX_DEPTH 6
static dir_t g_find_pool[FIND_MAX_DEPTH];
static int   g_find_hits = 0;

static void find_in_dir(dir_t* d, const char* prefix, const char* name, int depth) {
    if (depth >= FIND_MAX_DEPTH) return;

    for (u32 i = 0; i < d->num_entries; i++) {
        fat_dir_entry_t* e = (fat_dir_entry_t*)(d->entries + i * 32);
        if (e->name[0] == 0x00) return;
        if (e->name[0] == 0xE5) continue;
        if (e->attr & 0x08) continue;
        if (e->attr == 0x0F) continue;
        if (is_dot_entry(e)) continue;

        /* Build "NAME.EXT" display form */
        char disp[14];
        int p = 0;
        for (int j = 0; j < 8 && e->name[j] != ' '; j++) disp[p++] = e->name[j];
        int has_ext = 0;
        for (int j = 0; j < 3; j++) if (e->ext[j] != ' ') has_ext = 1;
        if (has_ext) {
            disp[p++] = '.';
            for (int j = 0; j < 3 && e->ext[j] != ' '; j++) disp[p++] = e->ext[j];
        }
        disp[p] = '\0';

        /* Build full path */
        char full[128];
        int fp = 0;
        for (int j = 0; prefix[j] && fp < 120; j++) full[fp++] = prefix[j];
        if (fp < 120) full[fp++] = '\\';
        for (int j = 0; disp[j] && fp < 126; j++) full[fp++] = disp[j];
        full[fp] = '\0';

        /* Match? */
        if (entry_name_matches(e, name)) {
            terminal_write("C:");
            terminal_write(full);
            terminal_write("\n");
            g_find_hits++;
        }

        /* Recurse into directory */
        if ((e->attr & 0x10) && e->low_cluster >= 2) {
            dir_t* sub = &g_find_pool[depth];
            if (dir_read(e->low_cluster, sub) == 0) {
                find_in_dir(sub, full, name, depth + 1);
            }
        }
    }
}

int fat12_find_recursive(const char* name) {
    g_find_hits = 0;
    dir_t* root = &g_find_pool[0];
    if (dir_read(0, root) < 0) return 0;
    find_in_dir(root, "", name, 1);
    return g_find_hits;
}
