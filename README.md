# MYOS

A small DOS-like operating system written from scratch in C and 32-bit x86
assembly. Bootable in QEMU.

## Features

- FAT16 filesystem with read/write and seekable file I/O
- 32 MB disk image
- VGA text mode (80x25) and mode 13h graphics (320x200, 256 colors)
- PS/2 keyboard and mouse drivers
- Sound Blaster 16 audio driver
- Programmable Interval Timer at 100 Hz
- Preemptive multitasking with a real heap allocator (`kmalloc` / `kfree`)
- C library: `string.h`, `stdlib.h`, `math.h`, `stdio.h`, `ctype.h`
- Command shell with:
  - Built-in commands (`DIR`, `CD`, `TYPE`, `COPY`, `DEL`, `MD`, `RD`, `REN`, `FIND`, `EDIT`, ...)
  - Batch scripts (`.BAT`) with `@ECHO OFF` and `%ENV%` variable expansion
  - Persistent settings in `\MYOS.CFG` (`SET`, `UNSET`, `CONFIG`)
  - Command history and line editing
- Program loader for flat `.COM` binaries with a syscall table (v11)

## Bundled programs

| Name | Description |
|---|---|
| `HELLO` | Minimal "hello world" program |
| `ECHO` | Echo a line back |
| `CALC` | Interactive calculator |
| `EDIT` | Line-based text editor |
| `SNAKE` | Classic snake game |
| `BALL` | Bouncing ball graphics demo (mode 13h) |
| `MOUSE` | Mouse pointer demo |
| `CLOCK` | Persistent clock in the top-right corner |
| `BEEP` | Play a tone through the SB16 |
| `SEEK` | Test the seekable file I/O syscalls |
| `HEAP` | Stress-test the heap allocator |
| `CTEST` | Exercise the C library |
| `CASTLE` | First-person raycaster (Wolfenstein-style) |

## Building

Requires macOS with Homebrew:

```sh
brew install nasm qemu xorriso llvm lld
make
make run
