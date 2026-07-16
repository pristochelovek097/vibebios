#ifndef DRV_ATA_H
#define DRV_ATA_H

#include "io.h"

// Read sectors from ATA drive using PIO
void ata_read_sectors(u8* buffer, u32 lba, u8 count);

#endif
