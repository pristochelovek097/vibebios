#ifndef DRV_KBD_H
#define DRV_KBD_H

#include "io.h"

// Flush any pending keyboard data in the controller
void kbd_flush();

// Initialize the keyboard controller
void kbd_init();

// Read a scancode if available, returns 0 if empty
u8 kbd_read_scancode();

// Mouse
void mouse_init();
int mouse_read_packet(char* dx, char* dy, u8* btn);

#endif
