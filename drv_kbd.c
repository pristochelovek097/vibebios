#include "drv_kbd.h"

void kbd_flush() {
    // Read from the keyboard buffer until it's empty
    while(inb(0x64) & 1) {
        inb(0x60);
    }
}

u8 kbd_read_scancode() {
    u8 status = inb(0x64);
    if((status & 1) && !(status & 0x20)) {
        return inb(0x60);
    }
    return 0;
}

static void kbd_wait_write() {
    for (int i = 0; i < 10000; i++) {
        if ((inb(0x64) & 2) == 0) return;
    }
}

static void kbd_wait_read() {
    for (int i = 0; i < 10000; i++) {
        if (inb(0x64) & 1) return;
    }
}

static u8 kbd_send_cmd(u8 cmd) {
    kbd_wait_write();
    outb(0x60, cmd);
    kbd_wait_read();
    return inb(0x60); // Read ACK or response
}

void kbd_init() {
    while(inb(0x64) & 2);
    outb(0x64, 0xAE); // Enable first PS/2 port
    
    // Reset kbd
    while(inb(0x64) & 2);
    outb(0x60, 0xFF);
    
    // Wait for ACK
    for(volatile int i=0; i<100000; i++) {
        if(inb(0x64) & 1) {
            if(inb(0x60) == 0xFA) break;
        }
    }
    
    u8 bat = inb(0x60); // Read BAT completion code (0xAA)

    // 6. Enable keyboard IRQ in controller
    kbd_wait_write(); outb(0x64, 0x60); // Write Command Byte
    kbd_wait_write(); outb(0x60, 0x47); // IRQ1=1, Translate=1

    // 7. Enable keyboard scanning
    kbd_send_cmd(0xF4);

    // 8. Enable keyboard in controller
    kbd_wait_write(); outb(0x64, 0xAE);
}

// --- PS/2 MOUSE ---

static void mouse_wait(u8 a_type) {
    u32 timeout = 10000000; // Increased timeout for QEMU
    if(a_type == 0) {
        while(timeout--) {
            if((inb(0x64) & 1) == 1) return;
        }
    } else {
        while(timeout--) {
            if((inb(0x64) & 2) == 0) return;
        }
    }
}

static void mouse_write(u8 a_write) {
    mouse_wait(1);
    outb(0x64, 0xD4); // Command: write to mouse
    mouse_wait(1);
    outb(0x60, a_write);
}

static u8 mouse_read() {
    mouse_wait(0);
    return inb(0x60);
}

void mouse_init() {
    mouse_wait(1);
    outb(0x64, 0xA8); // Enable Aux port
    
    mouse_wait(1);
    outb(0x64, 0x20); // Read Compaq status
    mouse_wait(0);
    u8 status = inb(0x60) | 2; // Enable IRQ12
    mouse_wait(1);
    outb(0x64, 0x60);
    mouse_wait(1);
    outb(0x60, status);
    
    // Tell mouse to use default settings
    mouse_write(0xF6);
    mouse_read(); // ACK
    
    // Enable Data Reporting
    mouse_write(0xF4);
    mouse_read(); // ACK
}

static u8 mouse_read_byte_sync() {
    u32 timeout = 10000000;
    while (timeout--) {
        u8 status = inb(0x64);
        if ((status & 0x21) == 0x21) {
            return inb(0x60); // Mouse byte
        }
        if ((status & 0x21) == 0x01) {
            inb(0x60); // Keyboard byte! Drop it to unblock the controller
        }
    }
    return 0;
}

int mouse_read_packet(char* dx, char* dy, u8* btn) {
    // We only process if a mouse byte is available
    if ((inb(0x64) & 0x21) == 0x21) { 
        u8 flags = inb(0x60);
        
        // Mouse packet alignment check (bit 3 must be 1)
        if (!(flags & 0x08)) {
            return 0; // Desynced, drop byte
        }
        
        u8 x = mouse_read_byte_sync();
        u8 y = mouse_read_byte_sync();
        
        *dx = x;
        *dy = y;
        if (flags & 0x10) *dx |= 0xFFFFFF00; 
        if (flags & 0x20) *dy |= 0xFFFFFF00; 
        *btn = flags & 0x07; 
        return 1;
    }
    return 0;
}
