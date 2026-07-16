#ifndef DRV_PXE_H
#define DRV_PXE_H

#include "io.h"

// Check if a network card is present and ready for PXE boot
int pxe_is_ready();

// Attempt to boot via PXE
int pxe_boot();

#endif
