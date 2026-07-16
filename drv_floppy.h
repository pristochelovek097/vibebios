#ifndef DRV_FLOPPY_H
#define DRV_FLOPPY_H

#include "io.h"

// Check if a floppy disk is inserted
int floppy_is_ready();

// Stub for reading sectors from floppy
int floppy_read_sectors(u8* buffer, u32 lba, u8 count);

#endif
