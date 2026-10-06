#ifndef ATA_H
#define ATA_H

int ata_read_sectors (unsigned int lba, unsigned char count, void* buffer);
int ata_write_sectors(unsigned int lba, unsigned char count, const void* buffer);

#endif
