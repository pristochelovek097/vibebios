#include "drv_pci.h"
#include "drv_kbd.h"
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define SCREEN_W 800
#define SCREEN_H 600

// --- ПОРТЫ С ЗАЩИТОЙ ОТ ОПТИМИЗАЦИИ GCC ---
#include "io.h"

// ЖЕСТКИЙ АДРЕС ВИДЕОПАМЯТИ: Компилятор не сможет это сломать
#define LFB_ADDR 0xE0000000
#define LFB ((volatile u32*)LFB_ADDR)

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

    // Insyde GUI Palette
    outb(0x3C8, 100); outb(0x3C9, 48); outb(0x3C9, 48); outb(0x3C9, 48); // 100 - Light Gray
    outb(0x3C8, 101); outb(0x3C9, 0);  outb(0x3C9, 0);  outb(0x3C9, 32); // 101 - Dark Blue
    outb(0x3C8, 102); outb(0x3C9, 63); outb(0x3C9, 63); outb(0x3C9, 63); // 102 - White
    outb(0x3C8, 103); outb(0x3C9, 0);  outb(0x3C9, 0);  outb(0x3C9, 0);  // 103 - Black
    outb(0x3C8, 104); outb(0x3C9, 32); outb(0x3C9, 32); outb(0x3C9, 32); // 104 - Dark Gray

    // 5. Настраиваем экран QEMU (Bochs VBE)
    outw(0x01CE, 0); outw(0x01CF, 0xB0C5); // Требуем LFB
    outw(0x01CE, 4); outw(0x01CF, 0);      // Отключаем
    
    outw(0x01CE, 1); outw(0x01CF, SCREEN_W); 
    outw(0x01CE, 2); outw(0x01CF, SCREEN_H); 
    outw(0x01CE, 6); outw(0x01CF, SCREEN_W); 
    outw(0x01CE, 7); outw(0x01CF, SCREEN_H); 
    outw(0x01CE, 3); outw(0x01CF, 32);        
    
    // Включаем + LFB (0x41)
    outw(0x01CE, 4); outw(0x01CF, 0x41); 
    inb(0x3DA); outb(0x3C0, 0x20);
}

// --- РИСОВАЛКИ ---
static void put_pixel(int x, int y, u32 color) {
    if(x>=0 && x<SCREEN_W && y>=0 && y<SCREEN_H) LFB[y * SCREEN_W + x] = color;
}

static void fill_rect(int x, int y, int w, int h, u32 color) {
    for(int j = y; j < y + h; j++) for(int i = x; i < x + w; i++) put_pixel(i, j, color);
}

static int mouse_x = 400;
static int mouse_y = 300;
static u32 bg_buffer[10*15];

static void save_mouse_bg() {
    for(int j=0; j<15; j++) {
        for(int i=0; i<10; i++) {
            if(mouse_y+j < SCREEN_H && mouse_x+i < SCREEN_W)
                bg_buffer[j*10+i] = LFB[(mouse_y+j)*SCREEN_W + (mouse_x+i)];
        }
    }
}
static void restore_mouse_bg() {
    for(int j=0; j<15; j++) {
        for(int i=0; i<10; i++) {
            if(mouse_y+j < SCREEN_H && mouse_x+i < SCREEN_W)
                LFB[(mouse_y+j)*SCREEN_W + (mouse_x+i)] = bg_buffer[j*10+i];
        }
    }
}
static void draw_mouse_cursor() {
    const char* cursor = 
    "X         "
    "XX        "
    "X.X       "
    "X..X      "
    "X...X     "
    "X....X    "
    "X.....X   "
    "X......X  "
    "X.......X "
    "X........X"
    "X.....XXXX"
    "X..X..X   "
    "X.X X..X  "
    "XX  X..X  "
    "X    XX   ";
    for(int j=0; j<15; j++) {
        for(int i=0; i<10; i++) {
            char c = cursor[j*10+i];
            if(c == 'X') put_pixel(mouse_x+i, mouse_y+j, 0xFF000000);
            if(c == '.') put_pixel(mouse_x+i, mouse_y+j, 0xFFFFFFFF);
        }
    }
}

#include "font_sun8x16_clean.h"
#include "font_ibm8x16.h"
#include "font_fixedsys8x16.h"

static const u8* current_font = font_ibm8x16; // default font
int ide_en = 1;
int acpi_en = 1;
int usb_en = 1;

static void draw_char(int x, int y, unsigned char c, u32 color) {
    for(int row = 0; row < 16; row++) {
        for(int col = 0; col < 8; col++) {
            if(current_font[(int)c * 16 + row] & (0x80 >> col)) {
                put_pixel(x + col, y + row, color);
            }
        }
    }
}

static void draw_string(int x, int y, const char* str, u32 color) {
    while(*str) {
        draw_char(x, y, *str++, color);
        x += 8;
    }
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
        draw_string(280, 500, "BOOTING HARD DRIVE 0", 0xFFFFFFFF);
        for(volatile int j = 0; j < 30000000; j++);
        ata_read_sectors((u8*)0x7C00, 0, 16);
    } else if (boot_device == 0x00) {
        draw_string(280, 500, "BOOTING FLOPPY DRIVE 0", 0xFFFFFFFF);
        for(volatile int j = 0; j < 30000000; j++);
        floppy_read_sectors((u8*)0x7C00, 0, 16);
    } else if (boot_device == 0x12) {
        draw_string(280, 500, "BOOTING NETWORK (PXE)", 0xFFFFFFFF);
        for(volatile int j = 0; j < 30000000; j++);
        if (!pxe_boot()) {
            draw_string(250, 540, "PXE ROM LOAD FAILED", 0xFFFF0000);
            return;
        }
    }

    if (boot_device != 0x12) {
        u16 signature = *(u16*)(0x7C00 + 510);
        if(signature != 0xAA55) {
            draw_string(240, 340, "NO BOOTABLE OS FOUND", 0xFFFF0000);
            return;
        }
    }

    draw_string(232, 340, "BOOT SIGNATURE FOUND!", 0xFF00FF00); 
    draw_string(296, 380, "BOOTING OS...", 0xFF00FFFF);
    
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
        draw_string(200, 360, "VGA ROM EXTRACTION FAILED!", 0xFFFF0000); 
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
void rtc_write(u8 index, u8 val) {
    outb(0x70, index);
    outb(0x71, val);
}

void save_cmos_settings() {
    u8 b1 = (ide_en & 1) | ((acpi_en & 1) << 1) | ((usb_en & 1) << 2);
    rtc_write(0x30, b1);
    
    u8 font_idx = 0;
    if(current_font == font_fixedsys8x16) font_idx = 1;
    if(current_font == font_sun8x16) font_idx = 2;
    rtc_write(0x31, font_idx);
    
    rtc_write(0x32, 0xAA);
}

void load_cmos_settings() {
    u8 magic = rtc_read(0x32);
    if(magic == 0xAA) {
        u8 b1 = rtc_read(0x30);
        ide_en = b1 & 1;
        acpi_en = (b1 >> 1) & 1;
        usb_en = (b1 >> 2) & 1;
        
        u8 font_idx = rtc_read(0x31);
        if(font_idx == 0) current_font = font_ibm8x16;
        if(font_idx == 1) current_font = font_fixedsys8x16;
        if(font_idx == 2) current_font = font_sun8x16;
    }
}

u8 bcd_to_bin(u8 bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}
u8 bin_to_bcd(u8 bin) {
    return ((bin / 10) << 4) | (bin % 10);
}

static void get_qemu_uuid_str(char* out) {
    outw(0x510, 0x0203); // FW_CFG_UUID
    u8 uuid[16];
    for (int i = 0; i < 16; i++) uuid[i] = inb(0x511);
    const char* hex = "0123456789ABCDEF";
    int p = 0;
    for (int i = 0; i < 16; i++) {
        out[p++] = hex[uuid[i] >> 4];
        out[p++] = hex[uuid[i] & 0x0F];
        if (i == 3 || i == 5 || i == 7 || i == 9) out[p++] = '-';
    }
    out[p] = 0;
}

static void draw_time(int x, int y, u8 h, u8 m, u8 s, u32 color) {
    char buf[9];
    buf[0] = '0' + (h / 10); buf[1] = '0' + (h % 10); buf[2] = ':';
    buf[3] = '0' + (m / 10); buf[4] = '0' + (m % 10); buf[5] = ':';
    buf[6] = '0' + (s / 10); buf[7] = '0' + (s % 10); buf[8] = 0;
    
    fill_rect(x, y, 16*8, 32, 0xFF0000AA);
    draw_string(x, y, buf, color);
}

static void draw_loading_bar() {
    draw_string(320, 430, "LOADING...", 0xFF00FFFF);
    for (int i = 0; i <= 100; i++) {
        fill_rect(248, 478, 204, 24, 0xFF000000); 
        fill_rect(250, 480, 200, 20, 0xFFFFFFFF); 
        fill_rect(250, 480, i * 2, 20, 0xFFFF00FF); 
        
        u8 s = bcd_to_bin(rtc_read(0x00));
        u8 m = bcd_to_bin(rtc_read(0x02));
        u8 h = bcd_to_bin(rtc_read(0x04));
        draw_time(640, 20, h, m, s, 63); 
        
        if (i % 10 == 0) beep(1000 + i*10, 500000); 
        for(volatile int j = 0; j < 500000; j++); 
    }
    nosound();
}

static int show_popup_menu(const char* title, const char** options, int num_options, int default_sel) {
    int sel = default_sel;
    int w = 300;
    int h = 60 + num_options * 30;
    int x = (SCREEN_W - w) / 2;
    int y = (SCREEN_H - h) / 2;
    
    int title_len = 0;
    while(title[title_len]) title_len++;
    
redraw_popup:
    fill_rect(x+10, y+10, w, h, 0xFF000000); // Shadow
    fill_rect(x, y, w, h, 0xFFFFFFFF);       // Border (White)
    fill_rect(x+2, y+2, w-4, h-4, 0xFF0000AA); // Background (Blue)
    
    draw_string(x + (w - 8*title_len)/2, y + 20, title, 0xFFFFFFFF);
    
    for(int i = 0; i < num_options; i++) {
        if(i == sel) {
            fill_rect(x + 20, y + 50 + i * 30, w - 40, 24, 0xFFFFFFFF); // White selection bg
            draw_string(x + 30, y + 54 + i * 30, options[i], 0xFF000000); // Black text
        } else {
            draw_string(x + 30, y + 54 + i * 30, options[i], 0xFFFFFFFF); // White text
        }
    }
    
    save_mouse_bg();
    draw_mouse_cursor();
    
    while(1) {
        char m_dx, m_dy; u8 m_btn;
        if(mouse_read_packet(&m_dx, &m_dy, &m_btn)) {
            restore_mouse_bg();
            mouse_x += m_dx;
            mouse_y -= m_dy;
            if(mouse_x < 0) mouse_x = 0;
            if(mouse_x > 790) mouse_x = 790;
            if(mouse_y < 0) mouse_y = 0;
            if(mouse_y > 585) mouse_y = 585;
            
            if (m_btn & 1) { // Left click
                for (int i = 0; i < num_options; i++) {
                    if (mouse_y >= y + 50 + i * 30 && mouse_y <= y + 74 + i * 30) {
                        if (mouse_x >= x + 20 && mouse_x <= x + w - 20) {
                            if(sel == i) return i; // Double click -> select
                            sel = i;
                            goto redraw_popup;
                        }
                    }
                }
            }
            save_mouse_bg();
            draw_mouse_cursor();
        }
        
        u8 key = kbd_read_scancode();
        if(!key) {
            continue;
        }
        
        if(key == 0xE0) { while(!(key = kbd_read_scancode())); }
        if(key & 0x80) continue;
        
        if(key == 0x01) return -1; // ESC
        if(key == 0x1C || key == 0x39) return sel; // Enter, Space
        
        if(key == 0x50) { // Down
            sel = (sel + 1) % num_options;
            goto redraw_popup;
        }
        if(key == 0x48) { // Up
            sel = (sel + num_options - 1) % num_options;
            goto redraw_popup;
        }
    }
}

#include "logo_data.h"

static void draw_logo_faded(int x, int y, u32 brightness) {
    for(int row = 0; row < LOGO_HEIGHT; row++) {
        for(int col = 0; col < LOGO_WIDTH; col++) {
            u32 color = logo_data[row * LOGO_WIDTH + col];
            u32 a = (color >> 24) & 0xFF;
            if(a > 128) {
                u32 r = (color >> 16) & 0xFF;
                u32 g = (color >> 8) & 0xFF;
                u32 b = color & 0xFF;
                r = (r * brightness) / 255;
                g = (g * brightness) / 255;
                b = (b * brightness) / 255;
                put_pixel(x + col, y + row, 0xFF000000 | (r << 16) | (g << 8) | b);
            }
        }
    }
}

static inline void post(u8 code) {
    outb(0x80, code);
}

struct Note { u32 freq; u32 dur; };
static void play_melody_and_fade_logo() {
    struct Note melody[] = {
        {659, 10000000}, {587, 10000000}, {370, 20000000}, {415, 20000000},
        {554, 10000000}, {493, 10000000}, {293, 20000000}, {329, 20000000},
        {493, 10000000}, {440, 10000000}, {277, 20000000}, {329, 20000000},
        {440, 40000000}
    };
    
    u32 current_frame = 0;
    u32 total_frames = 352; // 13 notes * ~16-64 frames each

    for (int i = 0; i < 13; i++) {
        post(0x10 + i); // Вывод POST-кода во время игры ноты
        u32 div = 1193180 / melody[i].freq;
        outb(0x43, 0xB6);
        outb(0x42, (u8)(div & 0xFF));
        outb(0x42, (u8)(div >> 8));
        u8 tmp = inb(0x61);
        if (tmp != (tmp | 3)) outb(0x61, tmp | 3);
        
        u32 frames = melody[i].dur / 600000; 
        if (frames == 0) frames = 1;
        
        for (u32 f = 0; f < frames; f++) {
            u32 brightness = (current_frame * 255) / total_frames;
            if (brightness > 255) brightness = 255;
            
            draw_logo_faded((800 - LOGO_WIDTH) / 2, (600 - LOGO_HEIGHT) / 2 - 50, brightness);
            current_frame++;
            
            // Добавляем задержку, чтобы ноты играли с правильной скоростью!
            for(volatile u32 wait=0; wait<400000; wait++);
        }
    }
    nosound();
}

static const char* get_pci_device_name(u16 vendor, u16 device) {
    if (vendor == 0x8086 && device == 0x1237) return "Intel 440FX Host Bridge";
    if (vendor == 0x8086 && device == 0x7000) return "Intel PIIX3 ISA Bridge";
    if (vendor == 0x8086 && device == 0x7010) return "Intel PIIX3 IDE Controller";
    if (vendor == 0x8086 && device == 0x7113) return "Intel PIIX4 ACPI";
    if (vendor == 0x1234 && device == 0x1111) return "QEMU Standard VGA";
    if (vendor == 0x10EC && device == 0x8139) return "Realtek RTL8139 Ethernet";
    if (vendor == 0x1000 && device == 0x100E) return "Intel E1000 Ethernet";
    return "Unknown PCI Device";
}

extern void init_smbios();
extern void init_acpi();

static inline void cpuid(u32 leaf, u32 *eax, u32 *ebx, u32 *ecx, u32 *edx) {
    __asm__ __volatile__("cpuid" : "=a" (*eax), "=b" (*ebx), "=c" (*ecx), "=d" (*edx) : "0" (leaf));
}

static void get_cpu_name(char *name) {
    u32 eax, ebx, ecx, edx;
    u32 *ptr = (u32*)name;
    cpuid(0x80000000, &eax, &ebx, &ecx, &edx);
    if (eax < 0x80000004) {
        name[0] = 'U'; name[1] = 'n'; name[2] = 'k'; name[3] = 'n'; name[4] = 'o'; name[5] = 'w'; name[6] = 'n'; name[7] = 0;
        return;
    }
    cpuid(0x80000002, &ptr[0], &ptr[1], &ptr[2], &ptr[3]);
    cpuid(0x80000003, &ptr[4], &ptr[5], &ptr[6], &ptr[7]);
    cpuid(0x80000004, &ptr[8], &ptr[9], &ptr[10], &ptr[11]);
    name[48] = 0;
    int i = 0, j = 0;
    while (name[i] == ' ') i++;
    while (name[i]) {
        name[j++] = name[i++];
    }
    name[j] = 0;
}

static u32 get_ram_kb() {
    u32 ext_kb = rtc_read(0x30) | (rtc_read(0x31) << 8);
    u32 ext_64kb = rtc_read(0x34) | (rtc_read(0x35) << 8);
    if (ext_64kb) return 16384 + ext_64kb * 64;
    return 1024 + ext_kb;
}

static void u32_to_str(u32 val, char* out) {
    if (val == 0) {
        out[0] = '0';
        out[1] = 0;
        return;
    }
    char buf[12];
    int p = 0;
    while(val > 0) {
        buf[p++] = '0' + (val % 10);
        val /= 10;
    }
    int out_p = 0;
    for(int i = p - 1; i >= 0; i--) {
        out[out_p++] = buf[i];
    }
    out[out_p] = 0;
}

static void ata_identify(u8 drive, char* model) {
    outb(0x1F6, 0xA0 | (drive << 4));
    for(volatile int i=0; i<400; i++);
    outb(0x1F2, 0); outb(0x1F3, 0); outb(0x1F4, 0); outb(0x1F5, 0);
    outb(0x1F7, 0xEC);
    
    u8 status = inb(0x1F7);
    if (status == 0) { model[0] = 0; return; }
    while (inb(0x1F7) & 0x80);
    
    u8 mid = inb(0x1F4);
    u8 high = inb(0x1F5);
    if (mid == 0x14 && high == 0xEB) {
        outb(0x1F7, 0xA1);
        while (inb(0x1F7) & 0x80);
    }
    
    status = inb(0x1F7);
    if (status & 0x01) { model[0] = 0; return; }
    
    while (!(inb(0x1F7) & 0x08));
    
    u16 data[256];
    for(int i = 0; i < 256; i++) data[i] = inw(0x1F0);
    
    int p = 0;
    for(int i = 27; i < 47; i++) {
        model[p++] = (char)(data[i] >> 8);
        model[p++] = (char)(data[i] & 0xFF);
    }
    model[p] = 0;
    while(p > 0 && model[p-1] == ' ') { model[p-1] = 0; p--; }
}

// --- ГЛАВНАЯ ФУНКЦИЯ ---
void bios_main() {
    nosound();
    init_vga();
    pci_init();
    kbd_init();
    mouse_init();
    init_smbios();
    init_acpi();
    load_cmos_settings();

    // Start with black screen
    fill_rect(0, 0, 800, 600, 0xFF000000);
    
    // Play melody while fading in the logo
    play_melody_and_fade_logo();

    // After fade, draw the rest of the POST screen
    fill_rect(0, 0, SCREEN_W, 4, 0xFF00FFFF);        
    fill_rect(0, SCREEN_H-4, SCREEN_W, 4, 0xFFFF00FF); 
    fill_rect(0, 0, 4, SCREEN_H, 0xFF00FFFF);        
    fill_rect(SCREEN_W-4, 0, 4, SCREEN_H, 0xFFFF00FF); 
    
    draw_string(280, 250, "PIZDEC COOL VIBEBIOS", 0xFFFFFF00);
    draw_string(312, 330, "32-BIT HUINYA MODE", 0xFF00FF00);

    draw_loading_bar();

    draw_string(240, 560, "Press ESC for Boot Menu, F2 for Setup", 63);
    
    kbd_flush();

    int show_menu = 0;
    int show_setup = 0;
    u8 boot_device = 0x80;
    int dbg_x = 0;
    
    // VibeBIOS Colors
    u32 COLOR_BG = 0xFFCCCCCC;       // Light Gray
    u32 COLOR_TOP_BAR = 0xFF0000AA;  // Dark Blue
    u32 COLOR_BOT_BAR = 0xFF0000AA;  // Dark Blue
    u32 COLOR_TEXT_NORM = 0xFFFFFFFF; // White
    u32 COLOR_TEXT_SEL = 0xFF000000;  // Black
    u32 COLOR_SEL_BG = 0xFFFFFFFF;    // White background for selected item
    u32 COLOR_PANEL = 0xFF666666;     // Darker Gray for borders

main_loop:
    show_menu = 0;
    show_setup = 0;
    // Draw initial POST screen
    char uuid_msg[60] = "Machine UUID ";
    get_qemu_uuid_str(uuid_msg + 13);

    char cpu_name[50];
    get_cpu_name(cpu_name);
    
    char ide0[50];
    char ide1[50];
    ata_identify(0, ide0);
    ata_identify(1, ide1);
    
    char ram_str[20];
    u32_to_str(get_ram_kb(), ram_str);
    int p = 0; while(ram_str[p]) p++;
    ram_str[p++] = 'K'; ram_str[p++] = 'B'; ram_str[p++] = ' ';
    ram_str[p++] = 'O'; ram_str[p++] = 'K'; ram_str[p] = 0;

    fill_rect(0, 0, 800, 600, 0xFF000000);
    
    int logo_x = (800 - LOGO_WIDTH) / 2;
    int logo_y = (600 - LOGO_HEIGHT) / 2 - 50;
    
    // Draw fully bright logo
    draw_logo_faded(logo_x, logo_y, 255);
    
    int ascii_y = logo_y + LOGO_HEIGHT + 20;
    int ascii_x = (800 - 47 * 8) / 2;
    
    draw_string(ascii_x, ascii_y,      "  __     _____ ____  _____ ____ ___ ___  ____  ", 0xFF00FFFF);
    draw_string(ascii_x, ascii_y + 16, " \\ \\   / /_ _| __ )| ____| __ )_ _/ _ \\/ ___| ", 0xFFFF00FF);
    draw_string(ascii_x, ascii_y + 32, "  \\ \\ / / | ||  _ \\|  _| |  _ \\| | | | \\___ \\ ", 0xFFFFFF00);
    draw_string(ascii_x, ascii_y + 48, "   \\ V /  | || |_) | |___| |_) | | |_| |___) |", 0xFF00FFFF);
    draw_string(ascii_x, ascii_y + 64, "    \\_/  |___|____/|_____|____/___\\___/|____/", 0xFFFF00FF);
    
    // AMIBIOS style logs at the top-left
    int log_y = 0;
    draw_string(0, log_y, "VIBEBIOS(C) 2026 Huinya Software Corp.", 0xFFFFFFFF); log_y += 16;
    draw_string(0, log_y, "VibeBIOS ACPI BIOS Revision 3.1", 0xFFFFFFFF); log_y += 16;
    draw_string(0, log_y, "Warning: This BIOS contains huinya.", 0xFFFF0000); log_y += 32;
    
    char cpu_log[80] = "CPU : ";
    int p_c = 6, c_i = 0;
    while(cpu_name[c_i]) { cpu_log[p_c++] = cpu_name[c_i++]; }
    cpu_log[p_c] = 0;
    draw_string(0, log_y, cpu_log, 0xFFFFFFFF); log_y += 32;
    
    draw_string(0, log_y, ram_str, 0xFFFFFFFF); log_y += 32;
    
    char pm_log[80] = "Auto-Detecting Pri Master.. ";
    int pm_i = 0, pm_j = 28;
    if (ide0[0]) { while(ide0[pm_i]) pm_log[pm_j++] = ide0[pm_i++]; } 
    else { const char* nd = "Not Detected"; while(nd[pm_i]) pm_log[pm_j++] = nd[pm_i++]; }
    pm_log[pm_j] = 0;

    char ps_log[80] = "Auto-Detecting Pri Slave... ";
    int ps_i = 0, ps_j = 28;
    if (ide1[0]) { while(ide1[ps_i]) ps_log[ps_j++] = ide1[ps_i++]; } 
    else { const char* nd = "Not Detected"; while(nd[ps_i]) ps_log[ps_j++] = nd[ps_i++]; }
    ps_log[ps_j] = 0;
    
    draw_string(0, log_y, pm_log, 0xFFFFFFFF); log_y += 16;
    draw_string(0, log_y, ps_log, 0xFFFFFFFF); log_y += 16;
    draw_string(0, log_y, "Initializing USB Controllers .. Done.", 0xFFFFFFFF); log_y += 32;
    draw_string(0, log_y, uuid_msg, 0xFFFFFFFF);
    
    draw_string(0, 560, "Press <ESC> for Boot Menu, <F2> for VibeBIOS Setup", 0xFFFFFFFF);

    for(volatile u32 i = 0; i < 80000000; i++) {
        u8 scancode = kbd_read_scancode();
        if(scancode) {
            if(scancode == 0x01 || scancode == 0x76) { // ESC
                show_menu = 1;
                break;
            }
            if(scancode == 0x3C || scancode == 0x06) { // F2: Set1=0x3C, Set2=0x06
                show_setup = 1;
                break;
            }
        }
    }

    // removed local statics

    if(show_setup) {
        int backup_ide = ide_en;
        int backup_acpi = acpi_en;
        int backup_usb = usb_en;
        const u8* backup_font = current_font;
        
        int current_tab = 0; // 0=Main, 1=Advanced, 2=Boot, 3=Exit
        int selected_item = 0;
        
redraw_setup:
        restore_mouse_bg();
        // Update time if idle? We can't easily without a timer, so it updates on keypress
        fill_rect(0, 0, 800, 600, COLOR_BG);
        fill_rect(0, 0, 800, 32, COLOR_TOP_BAR);
        draw_string(308, 8, "VibeBIOS", COLOR_TEXT_NORM);
        draw_string(726, 8, "Rev. 3.0", COLOR_TEXT_NORM);
        
        // Tab bar
        int tabs_x[] = {50, 150, 280, 400};
        char* tabs[] = {"Main", "Advanced", "Boot", "Exit"};
        fill_rect(0, 32, 800, 24, COLOR_BG);
        for(int i=0; i<4; i++) {
            if(i == current_tab) {
                fill_rect(tabs_x[i]-5, 32, 80, 24, COLOR_SEL_BG);
                draw_string(tabs_x[i], 36, tabs[i], COLOR_TEXT_SEL);
            } else {
                draw_string(tabs_x[i], 36, tabs[i], COLOR_TEXT_SEL); // Text on light gray
            }
        }
        
        // Borders
        fill_rect(10, 60, 780, 2, COLOR_PANEL);
        fill_rect(500, 60, 2, 480, COLOR_PANEL);
        fill_rect(10, 540, 780, 2, COLOR_PANEL);
        
        // Bottom bar
        fill_rect(0, 560, 800, 40, COLOR_BOT_BAR);
        draw_string(10, 572, "F1 Help  ^v Select Item  F5/F6 Change Values  F9 Setup Defaults  F10 Save and Exit", COLOR_TEXT_NORM);

        // Content
        if(current_tab == 0) {
            // Main Tab
            draw_string(520, 80, "System Time and Date", COLOR_TEXT_SEL);
            draw_string(520, 100, "Configuration.", COLOR_TEXT_SEL);
            
            u8 s = bcd_to_bin(rtc_read(0x00));
            u8 m = bcd_to_bin(rtc_read(0x02));
            u8 h = bcd_to_bin(rtc_read(0x04));
            char tstr[11];
            tstr[0]='['; tstr[1]='0'+h/10; tstr[2]='0'+h%10; tstr[3]=':'; tstr[4]='0'+m/10; tstr[5]='0'+m%10; tstr[6]=':'; tstr[7]='0'+s/10; tstr[8]='0'+s%10; tstr[9]=']'; tstr[10]=0;
            
            u8 day = bcd_to_bin(rtc_read(0x07));
            u8 mo = bcd_to_bin(rtc_read(0x08));
            u8 yr = bcd_to_bin(rtc_read(0x09));
            char dstr[13];
            dstr[0]='['; dstr[1]='0'+mo/10; dstr[2]='0'+mo%10; dstr[3]='/'; dstr[4]='0'+day/10; dstr[5]='0'+day%10; dstr[6]='/'; dstr[7]='2'; dstr[8]='0'; dstr[9]='0'+yr/10; dstr[10]='0'+yr%10; dstr[11]=']'; dstr[12]=0;
            
            draw_string(20, 80, "System Time", COLOR_TEXT_SEL);
            draw_string(250, 80, tstr, (selected_item==0) ? 101 : COLOR_TEXT_SEL);
            
            draw_string(20, 100, "System Date", COLOR_TEXT_SEL);
            draw_string(250, 100, dstr, (selected_item==1) ? 101 : COLOR_TEXT_SEL);
            
            draw_string(20, 140, "System Memory", COLOR_TEXT_SEL);
            draw_string(250, 140, "640 KB", COLOR_TEXT_SEL);
            
            draw_string(20, 160, "Extended Memory", COLOR_TEXT_SEL);
            draw_string(250, 160, "511 MB", COLOR_TEXT_SEL);
            
            draw_string(20, 200, "Active Font", COLOR_TEXT_SEL);
            char* f_name = "IBM VGA 8x16";
            if(current_font == font_fixedsys8x16) f_name = "Fixedsys 8x16";
            if(current_font == font_sun8x16) f_name = "Sun 8x16";
            draw_string(250, 200, f_name, (selected_item==2) ? 101 : COLOR_TEXT_SEL);
        }
        else if(current_tab == 1) {
            draw_string(520, 80, "Advanced Settings", COLOR_TEXT_SEL);
            draw_string(20, 80, "IDE Configuration", COLOR_TEXT_SEL);
            draw_string(250, 80, ide_en ? "[Enabled]" : "[Disabled]", (selected_item==0) ? 101 : COLOR_TEXT_SEL);
            draw_string(20, 100, "ACPI Configuration", COLOR_TEXT_SEL);
            draw_string(250, 100, acpi_en ? "[Enabled]" : "[Disabled]", (selected_item==1) ? 101 : COLOR_TEXT_SEL);
            draw_string(20, 120, "USB Configuration", COLOR_TEXT_SEL);
            draw_string(250, 120, usb_en ? "[Enabled]" : "[Disabled]", (selected_item==2) ? 101 : COLOR_TEXT_SEL);
            
            draw_string(20, 160, "Coreboot PCI Hardware Tree:", COLOR_TEXT_SEL);
            
            // Render first 8 PCI devices as a tree
            for (int i = 0; i < 8; i++) {
                pci_device_t* pdev = pci_find_device_by_index(i);
                if (pdev) {
                    char buf[64];
                    const char* name = get_pci_device_name(pdev->vendor, pdev->device);
                    const char* hex = "0123456789ABCDEF";
                    
                    u8 bus = pdev->bdf >> 8;
                    u8 dev = (pdev->bdf >> 3) & 0x1F;
                    u8 func = pdev->bdf & 0x07;
                    
                    char bdf_str[10];
                    bdf_str[0] = hex[bus & 0xF]; bdf_str[1] = ':';
                    bdf_str[2] = hex[dev >> 4];  bdf_str[3] = hex[dev & 0xF]; bdf_str[4] = '.';
                    bdf_str[5] = hex[func & 0xF]; bdf_str[6] = 0;
                    
                    int p = 0;
                    if(i == 7 || !pci_find_device_by_index(i+1)) {
                        buf[p++] = '\\'; buf[p++] = '-'; buf[p++] = ' ';
                    } else {
                        buf[p++] = '|'; buf[p++] = '-'; buf[p++] = ' ';
                    }
                    
                    for(int j=0; bdf_str[j]; j++) buf[p++] = bdf_str[j];
                    buf[p++] = ' ';
                    
                    for(int j=0; name[j] && p < 60; j++) buf[p++] = name[j];
                    buf[p] = 0;
                    
                    draw_string(40, 180 + i * 16, buf, COLOR_TEXT_SEL);
                } else break;
            }
        }
        else if(current_tab == 2) {
            draw_string(520, 80, "Boot Settings", COLOR_TEXT_SEL);
            draw_string(20, 80, "Boot Priority Order", COLOR_TEXT_SEL);
            draw_string(40, 100, "1. IDE HDD", COLOR_TEXT_SEL);
            draw_string(40, 120, "2. CD-ROM", COLOR_TEXT_SEL);
            draw_string(40, 140, "3. FDD", COLOR_TEXT_SEL);
            draw_string(40, 160, "4. Network", COLOR_TEXT_SEL);
        }
        else if(current_tab == 3) {
            draw_string(520, 80, "Exit Setup", COLOR_TEXT_SEL);
            draw_string(20, 80, "Exit Saving Changes", (selected_item==0) ? 101 : COLOR_TEXT_SEL);
            draw_string(20, 100, "Exit Discarding Changes", (selected_item==1) ? 101 : COLOR_TEXT_SEL);
            draw_string(20, 120, "Load Setup Defaults", (selected_item==2) ? 101 : COLOR_TEXT_SEL);
        }
        
        save_mouse_bg();
        draw_mouse_cursor();

        while(1) {
            u8 key = kbd_read_scancode();
            
            char m_dx, m_dy; u8 m_btn;
            if(mouse_read_packet(&m_dx, &m_dy, &m_btn)) {
                restore_mouse_bg();
                mouse_x += m_dx;
                mouse_y -= m_dy; // PS/2 Y is inverted
                if(mouse_x < 0) mouse_x = 0;
                if(mouse_x > 790) mouse_x = 790;
                if(mouse_y < 0) mouse_y = 0;
                if(mouse_y > 585) mouse_y = 585;
                
                if (m_btn & 1) { // Left click
                    // Tab clicking
                    if (mouse_y >= 32 && mouse_y <= 56) {
                        if (mouse_x >= tabs_x[0] && mouse_x <= tabs_x[0]+80) current_tab = 0;
                        else if (mouse_x >= tabs_x[1] && mouse_x <= tabs_x[1]+80) current_tab = 1;
                        else if (mouse_x >= tabs_x[2] && mouse_x <= tabs_x[2]+80) current_tab = 2;
                        else if (mouse_x >= tabs_x[3] && mouse_x <= tabs_x[3]+80) current_tab = 3;
                        selected_item = 0;
                        goto redraw_setup;
                    }
                    // Item clicking
                    int old_sel = selected_item;
                    if(current_tab == 0) {
                        if(mouse_y >= 80 && mouse_y <= 96) selected_item = 0;
                        if(mouse_y >= 100 && mouse_y <= 116) selected_item = 1;
                        if(mouse_y >= 200 && mouse_y <= 216) selected_item = 2;
                        
                        // If they clicked on an already selected item, act like Enter
                        if(old_sel == selected_item && mouse_x >= 20) {
                            key = 0x1C; // Simulate Enter press to open popup
                            goto process_key;
                        }
                        goto redraw_setup;
                    }
                    if(current_tab == 1) {
                        if(mouse_y >= 80 && mouse_y <= 96) selected_item = 0;
                        if(mouse_y >= 100 && mouse_y <= 116) selected_item = 1;
                        if(mouse_y >= 120 && mouse_y <= 136) selected_item = 2;
                        
                        if(old_sel == selected_item && mouse_x >= 20) {
                            key = 0x1C; // Simulate Enter press to open popup
                            goto process_key;
                        }
                        goto redraw_setup;
                    }
                }
                save_mouse_bg();
                draw_mouse_cursor();
            }

            if(!key) {
                continue;
            }
            
process_key:
            
            if(key == 0xE0) {
                while(!(key = kbd_read_scancode())); // wait for extended code
            }
            if(key & 0x80) continue; // Ignore break codes (key release)
            
            if(key == 0x4D) { // Right Arrow
                current_tab = (current_tab + 1) % 4;
                selected_item = 0;
                goto redraw_setup;
            }
            if(key == 0x4B) { // Left Arrow
                current_tab = (current_tab + 3) % 4;
                selected_item = 0;
                goto redraw_setup;
            }
            if(key == 0x50) { // Down Arrow
                selected_item++;
                goto redraw_setup;
            }
            if(key == 0x48) { // Up Arrow
                if(selected_item > 0) selected_item--;
                goto redraw_setup;
            }
            
            // Mouse handling moved to the top of the loop!
            
            if(key == 0x3F || key == 0x40 || key == 0x4E || key == 0x4A || key == 0x1C || key == 0x39) { // F5, F6, +, -, Enter, Space
                int dir = (key == 0x40 || key == 0x4E) ? 1 : -1;
                if(current_tab == 0) {
                    if(selected_item == 0) { // Time (change hours)
                        u8 h = bcd_to_bin(rtc_read(0x04));
                        h = (h + dir + 24) % 24;
                        rtc_write(0x04, bin_to_bcd(h));
                    }
                    if(selected_item == 1) { // Date (change day)
                        u8 d = bcd_to_bin(rtc_read(0x07));
                        d = d + dir;
                        if(d < 1) d = 31;
                        if(d > 31) d = 1;
                        rtc_write(0x07, bin_to_bcd(d));
                    }
                    if(selected_item == 2) { // Font Popup
                        const char* opts[] = {"IBM VGA 8x16", "Fixedsys 8x16", "Sun 8x16"};
                        int def = 0;
                        if(current_font == font_fixedsys8x16) def = 1;
                        if(current_font == font_sun8x16) def = 2;
                        int res = show_popup_menu("Active Font", opts, 3, def);
                        if(res == 0) current_font = font_ibm8x16;
                        if(res == 1) current_font = font_fixedsys8x16;
                        if(res == 2) current_font = font_sun8x16;
                    }
                }
                if(current_tab == 1) {
                    const char* opts[] = {"Disabled", "Enabled"};
                    if(selected_item == 0) {
                        int res = show_popup_menu("IDE Configuration", opts, 2, ide_en);
                        if(res != -1) ide_en = res;
                    }
                    if(selected_item == 1) {
                        int res = show_popup_menu("ACPI Configuration", opts, 2, acpi_en);
                        if(res != -1) acpi_en = res;
                    }
                    if(selected_item == 2) {
                        int res = show_popup_menu("USB Configuration", opts, 2, usb_en);
                        if(res != -1) usb_en = res;
                    }
                }
                goto redraw_setup;
            }
            
            if(current_tab == 0 && selected_item == 2) { // Direct Font selection
                if(key == 0x02) { current_font = font_ibm8x16; goto redraw_setup; }
                if(key == 0x03) { current_font = font_fixedsys8x16; goto redraw_setup; }
                if(key == 0x04) { current_font = font_sun8x16; goto redraw_setup; }
            }
            
            if(key == 0x01 || (key == 0x1C && current_tab == 3 && selected_item == 1)) { // ESC -> Exit
                fill_rect(210, 210, 400, 150, 0xFF000000); // Shadow
                fill_rect(200, 200, 400, 150, 0xFFFFFFFF); // White border
                fill_rect(202, 202, 396, 146, 0xFF0000AA); // Blue inside
                draw_string(220, 220, "Setup Confirmation", 0xFFFFFFFF);
                draw_string(220, 260, "Quit without saving?", 0xFFFFFFFF);
                int dlg_sel = 0;
                while(1) {
                    fill_rect(295, 305, 70, 24, (dlg_sel==0)? 102 : 101); 
                    draw_string(300, 310, "[Yes]", (dlg_sel==0)? 103 : 102);
                    fill_rect(395, 305, 60, 24, (dlg_sel==1)? 102 : 101); 
                    draw_string(400, 310, "[No]", (dlg_sel==1)? 103 : 102);
                    
                    u8 dkey = kbd_read_scancode();
                    if(!dkey) continue;
                    if(dkey == 0xE0) { while(!(dkey = kbd_read_scancode())); }
                    if(dkey & 0x80) continue;
                    
                    if(dkey == 0x4D || dkey == 0x4B) dlg_sel ^= 1;
                    if(dkey == 0x1C) {
                        if(dlg_sel == 0) {
                            ide_en = backup_ide;
                            acpi_en = backup_acpi;
                            usb_en = backup_usb;
                            current_font = backup_font;
                            goto main_loop;
                        }
                        else goto redraw_setup;
                    }
                    if(dkey == 0x01) goto redraw_setup;
                    for(volatile int j=0; j<2000000; j++);
                }
            }
            if(key == 0x44 || (key == 0x1C && current_tab == 3 && selected_item == 0)) { // F10 -> Save and Exit
                fill_rect(210, 210, 400, 150, 0xFF000000); // Shadow
                fill_rect(200, 200, 400, 150, 0xFFFFFFFF); // White border
                fill_rect(202, 202, 396, 146, 0xFF0000AA); // Blue inside
                draw_string(220, 220, "Setup Confirmation", 0xFFFFFFFF);
                draw_string(220, 260, "Save configuration changes and exit now?", 0xFFFFFFFF);
                int dlg_sel = 0;
                while(1) {
                    fill_rect(295, 305, 70, 24, (dlg_sel==0)? 102 : 101); 
                    draw_string(300, 310, "[Yes]", (dlg_sel==0)? 103 : 102);
                    fill_rect(395, 305, 60, 24, (dlg_sel==1)? 102 : 101); 
                    draw_string(400, 310, "[No]", (dlg_sel==1)? 103 : 102);
                    
                    u8 dkey = kbd_read_scancode();
                    if(!dkey) continue;
                    if(dkey == 0xE0) { while(!(dkey = kbd_read_scancode())); }
                    if(dkey & 0x80) continue;
                    
                    if(dkey == 0x4D || dkey == 0x4B) dlg_sel ^= 1;
                    if(dkey == 0x1C) {
                        if(dlg_sel == 0) {
                            save_cmos_settings();
                            goto main_loop;
                        }
                        else goto redraw_setup;
                    }
                    if(dkey == 0x01) goto redraw_setup;
                    for(volatile int j=0; j<2000000; j++);
                }
            }
            
            // Wait a little bit to prevent ultra-fast scrolling
            for(volatile int j=0; j<2000000; j++);
        }
    }

    if(show_menu) {
        fill_rect(0, 0, 800, 600, 0xFF0000AA); 
        draw_string(280, 100, "VIBEBIOS BOOT MENU", 0xFF00FFFF);
        draw_string(250, 150, "1. ATA HARD DRIVE 0", 0xFFFFFFFF);
        draw_string(250, 180, "2. FLOPPY DRIVE 0", 0xFFFFFFFF);
        draw_string(250, 210, "3. NETWORK BOOT (PXE)", 0xFFFFFFFF);
        draw_string(250, 260, "Select device (1-3):", 0xFFFFFF00);

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
        fill_rect(0, 0, 800, 600, 0xFF0000AA); 
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
