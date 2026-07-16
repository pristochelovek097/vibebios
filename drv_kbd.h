#ifndef DRV_KBD_H
#define DRV_KBD_H

#include "io.h"

// Flush any pending keyboard data in the controller
void kbd_flush();

// Read a scancode if available, returns 0 if empty
u8 kbd_read_scancode();

#endif
