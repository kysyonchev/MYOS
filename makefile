CC = /opt/homebrew/opt/llvm/bin/clang
LD = /opt/homebrew/opt/lld/bin/ld.lld
OBJCOPY = /opt/homebrew/opt/llvm/bin/llvm-objcopy
NASM = nasm

CFLAGS = --target=i386-elf -ffreestanding -fno-pic -fno-pie -fno-stack-protector -I include -c
LDFLAGS = -m elf_i386 -T linker.ld

PROG_CFLAGS = --target=i386-elf -ffreestanding -fno-pic -fno-pie -fno-stack-protector -mno-sse -mno-sse2 -mno-mmx -mno-avx -I programs -c
PROG_LDFLAGS = -m elf_i386 -T programs/program.ld

C_SOURCES = $(wildcard kernel/*.c drivers/*.c fs/*.c shell/*.c)
ASM_SOURCES = $(wildcard kernel/*.asm)

C_OBJECTS = $(patsubst %.c, build/%.o, $(C_SOURCES))
ASM_OBJECTS = $(patsubst %.asm, build/%.o, $(ASM_SOURCES))
OBJECTS = $(ASM_OBJECTS) $(C_OBJECTS)

PROGS = hello calc edit snake clock ball castle
PROG_OBJS = $(patsubst %, build/programs/%.o, $(PROGS))
PROG_ELFS = $(patsubst %, build/programs/%.elf, $(PROGS))
PROG_BINS = $(patsubst %, build/programs/%.bin, $(PROGS))

all: build/myos.bin $(PROG_BINS)

build/myos.bin: $(OBJECTS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

build/%.o: %.asm
	@mkdir -p $(dir $@)
	$(NASM) -f elf32 $< -o $@

build/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< -o $@

build/programs/%.o: programs/%.c programs/program.h
	@mkdir -p $(dir $@)
	$(CC) $(PROG_CFLAGS) $< -o $@

build/programs/%.elf: build/programs/%.o programs/program.ld
	$(LD) $(PROG_LDFLAGS) -o $@ $<

build/programs/%.bin: build/programs/%.elf
	$(OBJCOPY) -O binary $< $@

disk.img: tools/mkdisk.py $(PROG_BINS)
	python3 tools/mkdisk.py disk.img

run: build/myos.bin disk.img
	qemu-system-i386 -kernel build/myos.bin \
	  -drive file=disk.img,format=raw,if=ide,index=0 \
	  -m 32 -vga std \
	  -display cocoa,full-screen=on,zoom-to-fit=on

clean:
	rm -rf build disk.img

.PHONY: all run clean
