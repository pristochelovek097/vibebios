typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define SCREEN_W 800
#define SCREEN_H 600

// --- ПОРТЫ С ЗАЩИТОЙ ОТ ОПТИМИЗАЦИИ GCC ---
static inline void outb(u16 port, u8 val) { __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port) : "memory"); }
static inline u8 inb(u16 port) { u8 ret; __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port) : "memory"); return ret; }
static inline void outw(u16 port, u16 val) { __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port) : "memory"); }
static inline u16 inw(u16 port) { u16 ret; __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port) : "memory"); return ret; }
static inline void outl(u16 port, u32 val) { __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port) : "memory"); }
static inline u32 inl(u16 port) { u32 ret; __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port) : "memory"); return ret; }

// ЖЕСТКИЙ АДРЕС ВИДЕОПАМЯТИ: Компилятор не сможет это сломать
#define LFB_ADDR 0xE0000000
#define LFB ((volatile u8*)LFB_ADDR)

// --- PC SPEAKER ---
static void beep(u32 freq, u32 duration) {
    u32 div = 1193180 / freq;
    outb(0x43, 0xB6);
    outb(0x42, (u8)(div & 0xFF));
    outb(0x42, (u8)(div >> 8));
    u8 tmp = inb(0x61);
    if (tmp != (tmp | 3)) outb(0x61, tmp | 3);
    for(volatile u32 i = 0; i < duration; i++);
}
static void nosound() {
    u8 tmp = inb(0x61) & 0xFC;
    outb(0x61, tmp);
}

// --- ИНИЦИАЛИЗАЦИЯ НАСТОЯЩЕГО BIOS ---
void init_vga() {
    // 1. ВЫКЛЮЧАЕМ видеокарту перед настройкой (Command Reg = 0)
    outl(0xCF8, 0x80001004);
    outl(0xCFC, 0x00000000);

    // 2. ВПИСЫВАЕМ безопасный адрес 0xE0000000 в BAR0
    outl(0xCF8, 0x80001010);
    outl(0xCFC, LFB_ADDR);

    // 3. ВКЛЮЧАЕМ доступ к памяти и управление шиной (Command Reg = 0x07)
    outl(0xCF8, 0x80001004);
    outl(0xCFC, 0x00000007);

    // 4. Палитра
    outb(0x3C8, 0); 
    for (int i = 0; i < 256; i++) { outb(0x3C9, i / 4); outb(0x3C9, i / 4); outb(0x3C9, i / 4); }
    outb(0x3C8, 63); outb(0x3C9, 63); outb(0x3C9, 63); outb(0x3C9, 63); // 63 - Белый
    outb(0x3C8, 0);  outb(0x3C9, 0);  outb(0x3C9, 0);  outb(0x3C9, 0);  // 0 - Черный
    outb(0x3C8, 48); outb(0x3C9, 63); outb(0x3C9, 63); outb(0x3C9, 0);  // 48 - Желтый
    outb(0x3C8, 32); outb(0x3C9, 0);  outb(0x3C9, 63); outb(0x3C9, 0);  // 32 - Зеленый
    outb(0x3C8, 40); outb(0x3C9, 63); outb(0x3C9, 32); outb(0x3C9, 32); // 40 - Красный
    outb(0x3C8, 64); outb(0x3C9, 0); outb(0x3C9, 0); outb(0x3C9, 32); // 64 - Dark Blue
    outb(0x3C8, 65); outb(0x3C9, 0); outb(0x3C9, 63); outb(0x3C9, 63); // 65 - Cyan
    outb(0x3C8, 66); outb(0x3C9, 63); outb(0x3C9, 0); outb(0x3C9, 63); // 66 - Magenta

    // 5. Настраиваем экран QEMU (Bochs VBE)
    outw(0x01CE, 0); outw(0x01CF, 0xB0C5); // Требуем LFB
    outw(0x01CE, 4); outw(0x01CF, 0);      // Отключаем
    
    outw(0x01CE, 1); outw(0x01CF, SCREEN_W); 
    outw(0x01CE, 2); outw(0x01CF, SCREEN_H); 
    outw(0x01CE, 6); outw(0x01CF, SCREEN_W); 
    outw(0x01CE, 7); outw(0x01CF, SCREEN_H); 
    outw(0x01CE, 3); outw(0x01CF, 8);        
    
    // Включаем + LFB (0x41)
    outw(0x01CE, 4); outw(0x01CF, 0x41); 
    inb(0x3DA); outb(0x3C0, 0x20);
}

// --- РИСОВАЛКИ ---
static void put_pixel(int x, int y, u8 color) {
    LFB[y * SCREEN_W + x] = color;
}

static void fill_rect(int x, int y, int w, int h, u8 color) {
    for(int j = y; j < y + h; j++) for(int i = x; i < x + w; i++) put_pixel(i, j, color);
}

#include "font_sun8x16_clean.h"

static void draw_char(int x, int y, unsigned char c, u8 color) {
    for(int row = 0; row < 16; row++) {
        for(int col = 0; col < 8; col++) {
            if(font_sun8x16[(int)c * 16 + row] & (0x80 >> col)) {
                put_pixel(x + col*2, y + row*2, color);
                put_pixel(x + col*2 + 1, y + row*2, color);
                put_pixel(x + col*2, y + row*2 + 1, color);
                put_pixel(x + col*2 + 1, y + row*2 + 1, color);
            }
        }
    }
}
static void draw_string(int x, int y, const char* str, u8 color) {
    while(*str) { draw_char(x, y, *str, color); x += 16; str++; }
}

void load_vga_rom() {
    outl(0xCF8, 0x80000058);
    outl(0xCFC, 0x33333333);
    outl(0xCF8, 0x8000005C);
    outl(0xCFC, 0x33333333);

    u32 pci_addr = 0x80000000 | (0 << 16) | (2 << 11) | (0 << 8) | 0x30;
    outl(0xCF8, pci_addr);
    u32 orig_bar = inl(0xCFC);
    
    outl(0xCF8, pci_addr);
    outl(0xCFC, 0xFE000000 | 1);
    
    u32 cmd_addr = 0x80000000 | (0 << 16) | (2 << 11) | (0 << 8) | 0x04;
    outl(0xCF8, cmd_addr);
    u32 orig_cmd = inl(0xCFC);
    outl(0xCF8, cmd_addr);
    outl(0xCFC, orig_cmd | 2);
    
    u8* rom = (u8*)0xFE000000;
    if (rom[0] == 0x55 && rom[1] == 0xAA) {
        u32 size = rom[2] * 512;
        u8* dest = (u8*)0xC0000;
        for (u32 i = 0; i < size; i++) {
            dest[i] = rom[i];
        }
    }
    
    outl(0xCF8, cmd_addr);
    outl(0xCFC, orig_cmd);
    outl(0xCF8, pci_addr);
    outl(0xCFC, orig_bar);
}

// --- KEYBOARD ---
extern u8 kbd_read_scancode();
extern void kbd_flush();

// --- DISK ---
extern void ata_read_sectors(u8* buffer, u32 lba, u8 count);
extern void floppy_read_sectors(u8* buffer, u32 lba, u8 count);
extern int pxe_boot();
extern void copy_and_jump_16();

void search_os(u8 boot_device) {
    if (boot_device == 0x80) {
        draw_string(280, 500, "BOOTING HARD DRIVE 0", 63);
        for(volatile int j = 0; j < 30000000; j++);
        ata_read_sectors((u8*)0x7C00, 0, 16);
    } else if (boot_device == 0x00) {
        draw_string(280, 500, "BOOTING FLOPPY DRIVE 0", 63);
        for(volatile int j = 0; j < 30000000; j++);
        floppy_read_sectors((u8*)0x7C00, 0, 16);
    } else if (boot_device == 0x12) {
        draw_string(280, 500, "BOOTING NETWORK (PXE)", 63);
        for(volatile int j = 0; j < 30000000; j++);
        if (!pxe_boot()) {
            draw_string(250, 540, "PXE ROM LOAD FAILED", 40);
            return;
        }
    }

    if (boot_device != 0x12) {
        u16 signature = *(u16*)(0x7C00 + 510);
        if(signature != 0xAA55) {
            draw_string(240, 340, "NO BOOTABLE OS FOUND", 40);
            return;
        }
    }

    draw_string(232, 340, "BOOT SIGNATURE FOUND!", 32); 
    draw_string(296, 380, "BOOTING OS...", 65);
    
    for(volatile u32 i = 0; i < 50000000; i++); 
    
    u16* ivt = (u16*)0x0000;
    u8* dummy_iret = (u8*)0x0FFA;
    *dummy_iret = 0xCF; 
    
    for(int i = 0; i < 256; i++) {
        ivt[i*2] = 0x0FFA; 
        ivt[i*2+1] = 0x0000; 
    }

    load_vga_rom();
    
    u16 vga_sig = *(volatile u16*)0xC0000;
    if (vga_sig != 0xAA55) {
        draw_string(200, 360, "VGA ROM EXTRACTION FAILED!", 40); 
        while(1);
    }
    
    // Store boot_device in memory so boot.asm can read it
    *(volatile u8*)0x0500 = boot_device;
    
    copy_and_jump_16();
}

// --- RTC ---
u8 rtc_read(u8 index) {
    outb(0x70, index);
    return inb(0x71);
}

u8 bcd_to_bin(u8 bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

static void draw_time(int x, int y, u8 h, u8 m, u8 s, u8 color) {
    char buf[9];
    buf[0] = '0' + (h / 10); buf[1] = '0' + (h % 10); buf[2] = ':';
    buf[3] = '0' + (m / 10); buf[4] = '0' + (m % 10); buf[5] = ':';
    buf[6] = '0' + (s / 10); buf[7] = '0' + (s % 10); buf[8] = 0;
    
    fill_rect(x, y, 16*8, 32, 64);
    draw_string(x, y, buf, color);
}

static void draw_loading_bar() {
    draw_string(320, 430, "LOADING...", 65);
    for (int i = 0; i <= 100; i++) {
        fill_rect(248, 478, 204, 24, 0); 
        fill_rect(250, 480, 200, 20, 63); 
        fill_rect(250, 480, i * 2, 20, 66); 
        
        u8 s = bcd_to_bin(rtc_read(0x00));
        u8 m = bcd_to_bin(rtc_read(0x02));
        u8 h = bcd_to_bin(rtc_read(0x04));
        draw_time(640, 20, h, m, s, 63); 
        
        if (i % 10 == 0) beep(1000 + i*10, 500000); 
        for(volatile int j = 0; j < 500000; j++); 
    }
    nosound();
}

// --- ГЛАВНАЯ ФУНКЦИЯ ---
void bios_main() {
    init_vga();

    beep(523, 10000000); 
    beep(659, 10000000); 
    beep(784, 10000000); 
    beep(1046, 20000000); 

    fill_rect(0, 0, SCREEN_W, SCREEN_H, 64);
    
    fill_rect(0, 0, SCREEN_W, 4, 65);        
    fill_rect(0, SCREEN_H-4, SCREEN_W, 4, 66); 
    fill_rect(0, 0, 4, SCREEN_H, 65);        
    fill_rect(SCREEN_W-4, 0, 4, SCREEN_H, 66); 
    
    draw_string(332, 174, "VIBEBIOS", 0);  
    draw_string(328, 170, "VIBEBIOS", 65); 
    
    draw_string(280, 250, "VGA INITIALIZED", 48);
    draw_string(312, 330, "32-BIT MODE", 32);

    draw_loading_bar();

    draw_string(280, 560, "Press ESC for Boot Menu", 63);
    
    kbd_flush();

    int show_menu = 0;
    u8 boot_device = 0x80;
    int dbg_x = 0;

    for(volatile u32 i = 0; i < 80000000; i++) {
        u8 scancode = kbd_read_scancode();
        if(scancode) {
            char dbuf[4];
            dbuf[0] = "0123456789ABCDEF"[scancode >> 4];
            dbuf[1] = "0123456789ABCDEF"[scancode & 0x0F];
            dbuf[2] = ' ';
            dbuf[3] = 0;
            draw_string(280 + dbg_x, 500, dbuf, 48); 
            dbg_x += 24;
            if (dbg_x > 400) dbg_x = 0; 

            if(scancode == 0x01 || scancode == 0x76) { 
                show_menu = 1;
                break;
            }
        }
    }

    if(show_menu) {
        fill_rect(0, 0, 800, 600, 64); 
        draw_string(280, 100, "VIBEBIOS BOOT MENU", 65);
        draw_string(250, 150, "1. ATA HARD DRIVE 0", 63);
        draw_string(250, 180, "2. FLOPPY DRIVE 0", 63);
        draw_string(250, 210, "3. NETWORK BOOT (PXE)", 63);
        draw_string(250, 260, "Select device (1-3):", 48);

        while(1) {
            u8 key = kbd_read_scancode();
            if(key) {
                if(key == 0x02 || key == 0x16 || key == 0x4F) { 
                    boot_device = 0x80;
                    break;
                } else if(key == 0x03 || key == 0x1E || key == 0x50) { 
                    boot_device = 0x00;
                    break;
                } else if(key == 0x04 || key == 0x26 || key == 0x51) { 
                    boot_device = 0x12;
                    break;
                }
            }
        }
        fill_rect(0, 0, 800, 600, 64); 
    }

    search_os(boot_device);

    int color = 0;
    while(1) {
        color = (color + 1) % 64;
        if(color == 0) color = 1; 
        
        draw_string(296, 540, "SYSTEM HALTED", color);
        for(volatile int i = 0; i < 1000000; i++); 
    }
}
