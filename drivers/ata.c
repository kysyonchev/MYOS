#include "ata.h"

#define ATA_DATA       0x1F0
#define ATA_SECCOUNT   0x1F2
#define ATA_LBA_LOW    0x1F3
#define ATA_LBA_MID    0x1F4
#define ATA_LBA_HIGH   0x1F5
#define ATA_DRIVE      0x1F6
#define ATA_STATUS     0x1F7
#define ATA_COMMAND    0x1F7

#define ATA_SR_BSY  0x80
#define ATA_SR_ERR  0x01
#define ATA_SR_DF   0x20
#define ATA_SR_DRQ  0x08

#define ATA_CMD_READ_PIO   0x20
#define ATA_CMD_WRITE_PIO  0x30
#define ATA_CMD_FLUSH      0xE7

static inline unsigned char inb(unsigned short port) {
    unsigned char r;
    __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}
static inline void outb(unsigned short port, unsigned char v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}

static int ata_wait_ready(void) {
    for (int i = 0; i < 1000000; i++) {
        if (!(inb(ATA_STATUS) & ATA_SR_BSY)) return 0;
    }
    return -1;
}
static int ata_wait_drq(void) {
    for (int i = 0; i < 1000000; i++) {
        unsigned char s = inb(ATA_STATUS);
        if (s & ATA_SR_ERR) return -1;
        if (s & ATA_SR_DF)  return -1;
        if (s & ATA_SR_DRQ) return 0;
    }
    return -1;
}

static int ata_setup(unsigned int lba, unsigned char count, unsigned char cmd) {
    if (ata_wait_ready() < 0) return -1;
    outb(ATA_DRIVE,    0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECCOUNT, count);
    outb(ATA_LBA_LOW,  (unsigned char)(lba & 0xFF));
    outb(ATA_LBA_MID,  (unsigned char)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH, (unsigned char)((lba >> 16) & 0xFF));
    outb(ATA_COMMAND,  cmd);
    return 0;
}

int ata_read_sectors(unsigned int lba, unsigned char count, void* buffer) {
    /* ATA PIO is not reentrant: disable interrupts around the transfer. */
    __asm__ volatile ("cli");

    int result = 0;
    if (ata_setup(lba, count, ATA_CMD_READ_PIO) < 0) { result = -1; goto done; }

    unsigned short* p = (unsigned short*)buffer;
    for (int s = 0; s < count; s++) {
        if (ata_wait_drq() < 0) { result = -1; goto done; }
        for (int i = 0; i < 256; i++) {
            unsigned short word;
            __asm__ volatile ("inw %w1, %0" : "=a"(word) : "Nd"(ATA_DATA));
            *p++ = word;
        }
    }

done:
    __asm__ volatile ("sti");
    return result;
}

int ata_write_sectors(unsigned int lba, unsigned char count, const void* buffer) {
    /* Same reentrancy rule as read. */
    __asm__ volatile ("cli");

    int result = 0;
    if (ata_setup(lba, count, ATA_CMD_WRITE_PIO) < 0) { result = -1; goto done; }

    const unsigned short* p = (const unsigned short*)buffer;
    for (int s = 0; s < count; s++) {
        if (ata_wait_drq() < 0) { result = -1; goto done; }
        for (int i = 0; i < 256; i++) {
            unsigned short word = *p++;
            __asm__ volatile ("outw %0, %w1" : : "a"(word), "Nd"(ATA_DATA));
        }
    }

    outb(ATA_COMMAND, ATA_CMD_FLUSH);
    ata_wait_ready();

done:
    __asm__ volatile ("sti");
    return result;
}
