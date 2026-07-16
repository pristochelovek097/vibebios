#include "drv_floppy.h"

// The floppy controller is complex and requires DMA.
// For now, this is a stub that indicates no floppy is present.

int floppy_is_ready() {
    // Port 0x3F4 is the Main Status Register of the Floppy Controller
    // If it returns 0xFF, there's likely no controller.
    if(inb(0x3F4) == 0xFF) {
        return 0; // Not ready
    }
    
    // Even if controller exists, we assume no disk for now
    return 0;
}

int floppy_read_sectors(u8* buffer, u32 lba, u8 count) {
    (void)buffer;
    (void)lba;
    (void)count;
    return 0; // Failed
}
