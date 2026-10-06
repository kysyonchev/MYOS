#!/usr/bin/env python3
"""mkdisk.py - Build a 1.44 MB FAT12 floppy image with a directory tree."""

import sys, os, glob

SECTOR       = 512
SPT          = 18
HEADS        = 2
TRACKS       = 80
TOTAL_SECT   = SPT * HEADS * TRACKS

RESERVED     = 1
NUM_FATS     = 2
SPF          = 9
ROOT_ENTRIES = 224
ROOT_SECT    = (ROOT_ENTRIES * 32 + SECTOR - 1) // SECTOR
SPC          = 1

FAT1_SEC     = RESERVED
FAT2_SEC     = FAT1_SEC + SPF
ROOT_SEC     = FAT2_SEC + SPF
DATA_SEC     = ROOT_SEC + ROOT_SECT
DATA_SECTORS = TOTAL_SECT - DATA_SEC


def set_fat_entry(fat, cluster, value):
    off = (cluster * 3) // 2
    if cluster & 1:
        fat[off]     = (fat[off] & 0x0F) | ((value << 4) & 0xF0)
        fat[off + 1] = (value >> 4) & 0xFF
    else:
        fat[off]     = value & 0xFF
        fat[off + 1] = (fat[off + 1] & 0xF0) | ((value >> 8) & 0x0F)


def make_entry(name, ext, attr, cluster, size):
    e = bytearray(32)
    e[0:8]   = name.upper().ljust(8)[:8].encode('ascii')
    e[8:11]  = ext.upper().ljust(3)[:3].encode('ascii')
    e[11]    = attr
    e[26:28] = cluster.to_bytes(2, 'little')
    e[28:32] = size.to_bytes(4, 'little')
    return e


class DirWriter:
    """Wrapper for a subdirectory's 512-byte data block."""

    def __init__(self, builder, cluster, data):
        self.b = builder
        self.cluster = cluster
        self.data = bytearray(data)
        self.count = 2     # slot 0 = "." , slot 1 = ".."

    def add_file(self, name, ext, content):
        first = self.b.write_file_data(content)
        e = make_entry(name, ext, 0x20, first, len(content))
        off = self.count * 32
        self.data[off:off + 32] = e
        self.count += 1
        self.b.write_cluster(self.cluster, bytes(self.data))

    def add_dir(self, name):
        """Create a nested subdirectory inside this one."""
        sub_cluster = self.b.alloc()
        set_fat_entry(self.b.fat, sub_cluster, 0xFFF)

        data = bytearray(SECTOR * SPC)
        data[0:32]  = make_entry('.',  '', 0x10, sub_cluster, 0)
        data[32:64] = make_entry('..', '', 0x10, self.cluster, 0)
        self.b.write_cluster(sub_cluster, data)

        e = make_entry(name, '', 0x10, sub_cluster, 0)
        off = self.count * 32
        self.data[off:off + 32] = e
        self.count += 1
        self.b.write_cluster(self.cluster, bytes(self.data))

        return DirWriter(self.b, sub_cluster, data)


class Builder:
    def __init__(self):
        self.img = bytearray(TOTAL_SECT * SECTOR)
        self.fat = bytearray(SPF * SECTOR)
        self.fat[0] = 0xF0
        self.fat[1] = 0xFF
        self.fat[2] = 0xFF
        self.next_cluster = 2
        self.root = bytearray(ROOT_SECT * SECTOR)
        self.root_count = 0
        self._write_boot_sector()

    def _write_boot_sector(self):
        bs = bytearray(SECTOR)
        bs[0:3]   = b'\xEB\x3C\x90'
        bs[3:11]  = b'MYOS    '
        bs[11:13] = SECTOR.to_bytes(2, 'little')
        bs[13]    = SPC
        bs[14:16] = RESERVED.to_bytes(2, 'little')
        bs[16]    = NUM_FATS
        bs[17:19] = ROOT_ENTRIES.to_bytes(2, 'little')
        bs[19:21] = TOTAL_SECT.to_bytes(2, 'little')
        bs[21]    = 0xF0
        bs[22:24] = SPF.to_bytes(2, 'little')
        bs[24:26] = SPT.to_bytes(2, 'little')
        bs[26:28] = HEADS.to_bytes(2, 'little')
        bs[38]    = 0x29
        bs[39:43] = (0x12345678).to_bytes(4, 'little')
        bs[43:54] = b'MYOS DISK  '
        bs[54:62] = b'FAT12   '
        bs[510]   = 0x55
        bs[511]   = 0xAA
        self.img[0:SECTOR] = bs

    def alloc(self):
        c = self.next_cluster
        self.next_cluster += 1
        return c

    def write_cluster(self, cluster, data):
        data = data.ljust(SECTOR * SPC, b'\x00')[:SECTOR * SPC]
        sec = DATA_SEC + (cluster - 2) * SPC
        self.img[sec * SECTOR:(sec + SPC) * SECTOR] = data

    def write_file_data(self, content):
        size = len(content)
        if size == 0:
            return 0
        n = (size + SECTOR * SPC - 1) // (SECTOR * SPC)
        clusters = [self.alloc() for _ in range(n)]
        for i in range(len(clusters) - 1):
            set_fat_entry(self.fat, clusters[i], clusters[i + 1])
        set_fat_entry(self.fat, clusters[-1], 0xFFF)
        for i, c in enumerate(clusters):
            self.write_cluster(c, content[i * SECTOR * SPC:(i + 1) * SECTOR * SPC])
        return clusters[0]

    def add_file_root(self, name, ext, content):
        first = self.write_file_data(content)
        e = make_entry(name, ext, 0x20, first, len(content))
        off = self.root_count * 32
        self.root[off:off + 32] = e
        self.root_count += 1

    def make_dir_root(self, name):
        cluster = self.alloc()
        set_fat_entry(self.fat, cluster, 0xFFF)

        data = bytearray(SECTOR * SPC)
        data[0:32]  = make_entry('.',  '', 0x10, cluster, 0)
        data[32:64] = make_entry('..', '', 0x10, 0,       0)
        self.write_cluster(cluster, data)

        e = make_entry(name, '', 0x10, cluster, 0)
        off = self.root_count * 32
        self.root[off:off + 32] = e
        self.root_count += 1

        return DirWriter(self, cluster, data)

    def finalize(self, out):
        self.img[FAT1_SEC * SECTOR:(FAT1_SEC + SPF) * SECTOR] = self.fat
        self.img[FAT2_SEC * SECTOR:(FAT2_SEC + SPF) * SECTOR] = self.fat
        self.img[ROOT_SEC * SECTOR:(ROOT_SEC + ROOT_SECT) * SECTOR] = self.root
        with open(out, 'wb') as f:
            f.write(self.img)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else 'disk.img'
    b = Builder()

    # --- Root: system files ---
    b.add_file_root('MYOS', 'CFG',
        b'# MYOS.CFG - configuration file\r\n'
        b'HOSTNAME=MYOS\r\n'
        b'COLOR=7\r\n'
        b'# RUN=CLOCK\r\n')

    b.add_file_root('AUTOEXEC', 'BAT',
        b'@ECHO OFF\r\n'
        b'REM MYOS boot script\r\n'
        b'ENV USER=guest\r\n'
        b'ENV VERSION=0.6\r\n'
        b'ECHO Welcome to MYOS, %USER%!\r\n'
        b'ECHO Running MYOS version %VERSION%.\r\n'
        b'ECHO Type HELP for a list of commands.\r\n')

    # --- \PROGRAMS ---
    progs = b.make_dir_root('PROGRAMS')
    for binpath in sorted(glob.glob('build/programs/*.bin')):
        name = os.path.splitext(os.path.basename(binpath))[0].upper()[:8]
        with open(binpath, 'rb') as f:
            content = f.read()
        progs.add_file(name, 'COM', content)
        print(f'  PROGRAMS\\{name}.COM ({len(content)} bytes)')

    # --- \DOCS ---
    docs = b.make_dir_root('DOCS')
    docs.add_file('README', 'TXT',
        b'MYOS - a small DOS-like operating system.\r\n'
        b'\r\n'
        b'Commands are listed by HELP.\r\n'
        b'Programs live in \\PROGRAMS.\r\n'
        b'Use FIND <name> to locate any file.\r\n')
    docs.add_file('ABOUT', 'TXT',
        b'MYOS was written from scratch in C and assembly.\r\n'
        b'It runs in QEMU on an M1 Mac.\r\n')

    # --- \GAMES (empty placeholder) ---
    b.make_dir_root('GAMES')

    b.finalize(out)
    print(f'Wrote {out} ({len(b.img)} bytes)')


if __name__ == '__main__':
    main()
