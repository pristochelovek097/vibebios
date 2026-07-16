#include "drv_kbd.h"

void kbd_flush() {
    // Read from the keyboard buffer until it's empty
    while(inb(0x64) & 1) {
        inb(0x60);
    }
}

u8 kbd_read_scancode() {
    if(inb(0x64) & 1) {
        return inb(0x60);
    }
    return 0;
}
