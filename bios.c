#include "drv_pci.h"
#include "drv_kbd.h"
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define SCREEN_W 800
#define SCREEN_H 600

// --- ПОРТЫ С ЗАЩИТОЙ ОТ ОПТИМИЗАЦИИ GCC ---
#include "io.h"

// --- VIBEBOOT: NEW FEATURES ---
// CMOS Layout:
//   0x30: bit0=ide_en, bit1=acpi_en, bit2=usb_en
//   0x31: font_idx (0=IBM, 1=Fixedsys, 2=Sun)
//   0x32: magic 0xAA
//   0x33: boot_order (bits 3:0=dev1, bits 7:4=dev2)  (0=HDD,1=CD,2=FDD,3=NET)
//   0x34: boot_order continued (bits 3:0=dev3, bits 7:4=dev4)
//   0x35: password_enabled (0=no, 1=yes)
//   0x36-0x3F: password hash (10 bytes, simple XOR hash)
//   0x40-0x41: splash_enabled
//   0x42-0x4E: reserved
//   0x4F: extended magic 0xBB
#define CMOS_MAGIC_ADDR      0x32
#define CMOS_BOOT_ORDER_ADDR 0x33
#define CMOS_PASSWD_ADDR     0x35
#define CMOS_PASSWD_HASH     0x36
#define CMOS_PASSWD_LEN      10
#define CMOS_SPLASH_ADDR     0x40
#define CMOS_EXT_MAGIC_ADDR  0x4F

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

// --- Boot Order: 0=HDD, 1=CD, 2=FDD, 3=NET ---
int boot_order[4] = {0, 1, 2, 3}; // default order
int boot_order_count = 4;

// --- Boot Password ---
int password_enabled = 0;
u8 password_hash[CMOS_PASSWD_LEN]; // Simple XOR-based hash

// --- Boot Splash ---
int splash_enabled = 1;

// --- CPUID Cache ---
static char cpu_name_cached[49] = {0};
static u32 cpu_max_leaf = 0;
static u32 cpu_max_ext_leaf = 0;
static u32 cpu_features_edx = 0;
static u32 cpu_features_ecx = 0;
static u32 cpu_ext_features_edx = 0;

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

extern void ata_read_sectors(u8* buffer, u32 lba, u8 count);
extern void atapi_read_sectors(u8 drive, u8* buffer, u32 lba, u8 count);
extern void floppy_read_sectors(u8* buffer, u32 lba, u8 count);
extern int pxe_boot();
extern void copy_and_jump_16();

void search_os(u8 boot_device) {
    if (boot_device == 0x80) {
        draw_string(280, 500, "BOOTING HARD DRIVE 0", 0xFFFFFFFF);
        for(volatile int j = 0; j < 30000000; j++);
        ata_read_sectors((u8*)0x7C00, 0, 16);
    } else if (boot_device == 0xE0) {
        draw_string(280, 500, "BOOTING CD-ROM 0", 0xFFFFFFFF);
        for(volatile int j = 0; j < 30000000; j++);
        
        // El Torito Boot
        u8 sector[2048];
        atapi_read_sectors(1, sector, 17, 1); // Boot Record Volume Descriptor
        if (sector[0] != 0 || sector[1] != 'C' || sector[2] != 'D' || sector[3] != '0' || sector[4] != '0' || sector[5] != '1') {
            draw_string(240, 540, "NOT A BOOTABLE CD", 0xFFFF0000);
            return;
        }
        
        u32 catalog_lba = *(u32*)(sector + 0x47);
        atapi_read_sectors(1, sector, catalog_lba, 1); // Boot Catalog
        
        // Boot catalog contains Validation Entry (0x00-0x1F) and Initial/Default Entry (0x20-0x3F)
        u8 boot_media_type = sector[0x20 + 1];
        u16 sector_count = *(u16*)(sector + 0x20 + 6);
        u32 boot_lba = *(u32*)(sector + 0x20 + 8);
        
        if (boot_media_type != 0x00) { // We only support "No Emulation" for Windows XP
            draw_string(240, 540, "UNSUPPORTED EMULATION", 0xFFFF0000);
            return;
        }
        
        atapi_read_sectors(1, (u8*)0x7C00, boot_lba, sector_count);
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

    if (boot_device != 0x12 && boot_device != 0xE0) {
        u16 signature = *(u16*)(0x7C00 + 510);
        if(signature != 0xAA55) {
            draw_string(240, 340, "NO BOOTABLE OS FOUND", 0xFFFF0000);
            return;
        }
    }

    draw_string(232, 340, "BOOT SIGNATURE FOUND!", 0xFF00FF00); 
    draw_string(296, 380, "BOOTING OS...", 0xFF00FFFF);
    
    for(volatile u32 i = 0; i < 50000000; i++); 
    
    // The IVT is already properly configured by boot.asm's real-mode payload.
    // We must NOT wipe it, otherwise Windows XP / NTLDR will crash instantly
    // when trying to use INT 13h to read the disk.


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

    // Save boot order
    u8 bo_low = (boot_order[0] & 0x0F) | ((boot_order[1] & 0x0F) << 4);
    u8 bo_high = (boot_order[2] & 0x0F) | ((boot_order[3] & 0x0F) << 4);
    rtc_write(CMOS_BOOT_ORDER_ADDR, bo_low);
    rtc_write(CMOS_BOOT_ORDER_ADDR + 1, bo_high);

    // Save boot password
    rtc_write(CMOS_PASSWD_ADDR, password_enabled & 1);
    for(int i = 0; i < CMOS_PASSWD_LEN; i++)
        rtc_write(CMOS_PASSWD_HASH + i, password_hash[i]);

    // Save splash
    rtc_write(CMOS_SPLASH_ADDR, splash_enabled & 1);
    rtc_write(CMOS_EXT_MAGIC_ADDR, 0xBB);
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

        // Load boot order
        u8 bo_low = rtc_read(CMOS_BOOT_ORDER_ADDR);
        u8 bo_high = rtc_read(CMOS_BOOT_ORDER_ADDR + 1);
        boot_order[0] = bo_low & 0x0F;
        boot_order[1] = (bo_low >> 4) & 0x0F;
        boot_order[2] = bo_high & 0x0F;
        boot_order[3] = (bo_high >> 4) & 0x0F;

        // Load boot password
        password_enabled = rtc_read(CMOS_PASSWD_ADDR) & 1;
        for(int i = 0; i < CMOS_PASSWD_LEN; i++)
            password_hash[i] = rtc_read(CMOS_PASSWD_HASH + i);

        // Load splash setting
        u8 ext_magic = rtc_read(CMOS_EXT_MAGIC_ADDR);
        if(ext_magic == 0xBB) {
            splash_enabled = rtc_read(CMOS_SPLASH_ADDR) & 1;
        }
    }
}

u8 bcd_to_bin(u8 bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}
u8 bin_to_bcd(u8 bin) {
    return ((bin / 10) << 4) | (bin % 10);
}

// --- PASSWORD HASH (simple XOR mix) ---
static void hash_password(const char* pass, u8* hash_out) {
    u8 hash[CMOS_PASSWD_LEN];
    for(int i = 0; i < CMOS_PASSWD_LEN; i++) hash[i] = 0x5A;
    int len = 0;
    while(pass[len] && len < 32) len++;
    for(int i = 0; i < len; i++) {
        hash[i % CMOS_PASSWD_LEN] ^= pass[i];
        hash[(i + 3) % CMOS_PASSWD_LEN] += pass[i] ^ 0xA5;
        hash[(i + 7) % CMOS_PASSWD_LEN] ^= (pass[i] >> 1) | (pass[i] << 7);
    }
    // Mix passes
    for(int round = 0; round < 4; round++) {
        for(int i = 0; i < CMOS_PASSWD_LEN; i++) {
            hash[i] ^= hash[(i + 1) % CMOS_PASSWD_LEN];
            hash[i] += 0x37;
        }
    }
    for(int i = 0; i < CMOS_PASSWD_LEN; i++) hash_out[i] = hash[i];
}

static int check_password(const char* pass) {
    if(!password_enabled) return 1; // no password = always allow
    u8 h[CMOS_PASSWD_LEN];
    hash_password(pass, h);
    for(int i = 0; i < CMOS_PASSWD_LEN; i++) {
        if(h[i] != password_hash[i]) return 0;
    }
    return 1;
}

// --- CPUID CACHE ---
static void cache_cpuid() {
    if(cpu_name_cached[0]) return; // already cached
    u32 eax, ebx, ecx, edx;
    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "0"(0));
    cpu_max_leaf = eax;
    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "0"(1));
    cpu_features_edx = edx;
    cpu_features_ecx = ecx;
    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "0"(0x80000000));
    cpu_max_ext_leaf = eax;
    if(cpu_max_ext_leaf >= 0x80000004) {
        u32* ptr = (u32*)cpu_name_cached;
        asm volatile("cpuid" : "=a"(ptr[0]), "=b"(ptr[1]), "=c"(ptr[2]), "=d"(ptr[3]) : "0"(0x80000002));
        asm volatile("cpuid" : "=a"(ptr[4]), "=b"(ptr[5]), "=c"(ptr[6]), "=d"(ptr[7]) : "0"(0x80000003));
        asm volatile("cpuid" : "=a"(ptr[8]), "=b"(ptr[9]), "=c"(ptr[10]), "=d"(ptr[11]) : "0"(0x80000004));
        cpu_name_cached[48] = 0;
        // Trim leading spaces
        int s = 0;
        while(cpu_name_cached[s] == ' ') s++;
        if(s > 0) {
            int j = 0;
            while(cpu_name_cached[s]) cpu_name_cached[j++] = cpu_name_cached[s++];
            cpu_name_cached[j] = 0;
        }
    }
    if(cpu_max_ext_leaf >= 0x80000001) {
        asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "0"(0x80000001));
        cpu_ext_features_edx = edx;
    }
}

// --- BOOT DEVICE NAMES ---
static const char* boot_device_name(int dev) {
    switch(dev) {
        case 0: return "IDE Hard Drive";
        case 1: return "CD-ROM";
        case 2: return "Floppy Drive";
        case 3: return "Network (PXE)";
        default: return "Unknown";
    }
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
    if (vendor == 0x1AF4 && device == 0x1000) return "VirtIO Network";
    if (vendor == 0x1AF4 && device == 0x1001) return "VirtIO Block";
    if (vendor == 0x1AF4 && device == 0x1009) return "VirtIO SCSI";
    return "Unknown PCI Device";
}

static const char* get_pci_class_name(u16 class_code) {
    switch(class_code >> 8) {
        case 0x00: return "Legacy Device";
        case 0x01: return "Mass Storage";
        case 0x02: return "Network";
        case 0x03: return "Display";
        case 0x04: return "Multimedia";
        case 0x05: return "Memory";
        case 0x06: return "Bridge";
        case 0x07: return "Communication";
        case 0x08: return "System";
        case 0x09: return "Input";
        case 0x0A: return "Docking";
        case 0x0B: return "Processor";
        case 0x0C: return "Serial Bus";
        case 0x0D: return "Wireless";
        case 0x0E: return "Intelligent I/O";
        case 0x0F: return "Satellite";
        case 0x10: return "Encryption";
        case 0x11: return "Signal Processing";
        default:   return "Other";
    }
}

extern void init_smbios();
extern void init_acpi();

static inline void cpuid(u32 leaf, u32 *eax, u32 *ebx, u32 *ecx, u32 *edx) {
    __asm__ __volatile__("cpuid" : "=a" (*eax), "=b" (*ebx), "=c" (*ecx), "=d" (*edx) : "0" (leaf));
}

static void get_cpu_name(char *name) {
    cache_cpuid();
    int i = 0;
    while(cpu_name_cached[i]) { name[i] = cpu_name_cached[i]; i++; }
    name[i] = 0;
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
    
    if (drive == 0) {
        // Save LBA28 total sectors to 0x0504 for INT 13h AH=48h handler
        u32 total_sectors = ((u32)data[61] << 16) | data[60];
        *(volatile u32*)0x0504 = total_sectors;
    }
    
    // Restore selection to Primary Master so boot sector reading doesn't hang!
    outb(0x1F6, 0xA0);
}

// ============================================================
// VT100 TERMINAL EMULATOR — Full ANSI escape sequence support
// Scrollback buffer, 256-color, cursor control, Serial I/O
// ============================================================
#define TERM_COLS       80
#define TERM_ROWS       25
#define TERM_SCROLLBACK 2000
#define TERM_FONT_W     8
#define TERM_FONT_H     16

// VGA Text Mode memory (for reference/fallback)
#define VGA_TEXT_ADDR   0xB8000

typedef struct {
    u8 ch;
    u8 fg;
    u8 bg;
} term_cell_t;

// ANSI 16-color palette (ARGB)
static const u32 term_palette[16] = {
    0xFF000000, // 0: Black
    0xFF0000AA, // 1: Red
    0xFF00AA00, // 2: Green
    0xFF00AAAA, // 3: Yellow
    0xFFAA0000, // 4: Blue
    0xFFAA00AA, // 5: Magenta
    0xFFAA5500, // 6: Cyan
    0xFFAAAAAA, // 7: White
    0xFF555555, // 8: Bright Black (Gray)
    0xFFFF5555, // 9: Bright Red
    0xFF55FF55, // 10: Bright Green
    0xFFFFFF55, // 11: Bright Yellow
    0xFF5555FF, // 12: Bright Blue
    0xFFFF55FF, // 13: Bright Magenta
    0xFF55FFFF, // 14: Bright Cyan
    0xFFFFFFFF, // 15: Bright White
};

// Extended 256-color palette (216 RGB colors)
static u32 term_palette256[256];
static int term_palette256_init = 0;

static void term_init_palette256() {
    if(term_palette256_init) return;
    term_palette256_init = 1;
    for(int i = 0; i < 16; i++) term_palette256[i] = term_palette[i];
    // 216 RGB colors (indices 16-231)
    for(int i = 0; i < 216; i++) {
        int r = (i / 36) * 51;
        int g = ((i / 6) % 6) * 51;
        int b = (i % 6) * 51;
        term_palette256[16 + i] = 0xFF000000 | (r << 16) | (g << 8) | b;
    }
    // 24 grayscale (indices 232-255)
    for(int i = 0; i < 24; i++) {
        u8 v = 8 + i * 10;
        term_palette256[232 + i] = 0xFF000000 | (v << 16) | (v << 8) | v;
    }
}

// Terminal state
typedef struct {
    term_cell_t screen[TERM_ROWS][TERM_COLS];
    term_cell_t scrollback[TERM_SCROLLBACK];
    int scroll_pos;       // Current scroll position in history
    int scroll_count;     // Total lines in scrollback

    int cursor_x, cursor_y;
    int saved_cx, saved_cy;
    int cursor_visible;
    int cursor_blink_state;

    int current_fg, current_bg;
    int bold, underline, inverse, dim, italic;

    // ANSI parser state
    int ansi_state;       // 0=normal, 1=ESC received, 2='[' received, 3='?' received
    int ansi_params[16];
    int ansi_param_count;
    int ansi_private;     // ?-mode (DEC private)

    int auto_wrap;
    int origin_mode;      // DEC Origin Mode
    int insert_mode;
    int scroll_top, scroll_bottom;
    int tab_width;

    int wrap_pending;
} terminal_t;

static terminal_t term;

static void term_init() {
    term_init_palette256();
    for(int r = 0; r < TERM_ROWS; r++)
        for(int c = 0; c < TERM_COLS; c++) {
            term.screen[r][c].ch = ' ';
            term.screen[r][c].fg = 7;
            term.screen[r][c].bg = 0;
        }
    for(int i = 0; i < TERM_SCROLLBACK; i++) {
        term.scrollback[i].ch = ' ';
        term.scrollback[i].fg = 7;
        term.scrollback[i].bg = 0;
    }
    term.cursor_x = 0;
    term.cursor_y = 0;
    term.saved_cx = 0;
    term.saved_cy = 0;
    term.cursor_visible = 1;
    term.cursor_blink_state = 1;
    term.current_fg = 7;
    term.current_bg = 0;
    term.bold = 0;
    term.underline = 0;
    term.inverse = 0;
    term.dim = 0;
    term.italic = 0;
    term.ansi_state = 0;
    term.ansi_param_count = 0;
    term.ansi_private = 0;
    term.auto_wrap = 1;
    term.origin_mode = 0;
    term.insert_mode = 0;
    term.scroll_top = 0;
    term.scroll_bottom = TERM_ROWS - 1;
    term.tab_width = 8;
    term.scroll_pos = 0;
    term.scroll_count = 0;
    term.wrap_pending = 0;
}

static void term_scroll_up() {
    if(term.scroll_count < TERM_SCROLLBACK) term.scroll_count++;
    for(int i = 0; i < TERM_COLS; i++) {
        term.scrollback[term.scroll_count - 1] = term.screen[0][i];
    }
    for(int r = 0; r < TERM_ROWS - 1; r++)
        for(int c = 0; c < TERM_COLS; c++)
            term.screen[r][c] = term.screen[r + 1][c];
    for(int c = 0; c < TERM_COLS; c++) {
        term.screen[TERM_ROWS - 1][c].ch = ' ';
        term.screen[TERM_ROWS - 1][c].fg = term.current_fg;
        term.screen[TERM_ROWS - 1][c].bg = term.current_bg;
    }
}

static void term_scroll_down() {
    for(int r = TERM_ROWS - 1; r > 0; r--)
        for(int c = 0; c < TERM_COLS; c++)
            term.screen[r][c] = term.screen[r - 1][c];
    for(int c = 0; c < TERM_COLS; c++) {
        term.screen[0][c].ch = ' ';
        term.screen[0][c].fg = term.current_fg;
        term.screen[0][c].bg = term.current_bg;
    }
}

static void term_newline() {
    term.cursor_x = 0;
    if(term.cursor_y >= term.scroll_bottom) {
        term_scroll_up();
    } else {
        term.cursor_y++;
    }
}

static void term_put_char(u8 c) {
    if(c == '\n') {
        term_newline();
        return;
    }
    if(c == '\r') {
        term.cursor_x = 0;
        return;
    }
    if(c == '\t') {
        term.cursor_x = (term.cursor_x + term.tab_width) / term.tab_width * term.tab_width;
        if(term.cursor_x >= TERM_COLS) term.cursor_x = TERM_COLS - 1;
        return;
    }
    if(c == '\b') {
        if(term.cursor_x > 0) term.cursor_x--;
        return;
    }
    if(c == '\a') {
        beep(1000, 200000);
        return;
    }

    if(term.insert_mode) {
        for(int c2 = TERM_COLS - 1; c2 > term.cursor_x; c2--)
            term.screen[term.cursor_y][c2] = term.screen[term.cursor_y][c2 - 1];
    }

    int fg = term.current_fg;
    int bg = term.current_bg;
    if(term.inverse) { int tmp = fg; fg = bg; bg = tmp; }
    if(term.bold && fg < 8) fg += 8;
    if(term.dim && fg >= 8) fg -= 8;

    term.screen[term.cursor_y][term.cursor_x].ch = c;
    term.screen[term.cursor_y][term.cursor_x].fg = fg;
    term.screen[term.cursor_y][term.cursor_x].bg = bg;

    term.cursor_x++;
    if(term.cursor_x >= TERM_COLS) {
        term.cursor_x = 0;
        if(term.cursor_y >= term.scroll_bottom) {
            term_scroll_up();
        } else {
            term.cursor_y++;
        }
    }
}

static void term_clear_line(int row, int start_col, int end_col, int mode) {
    for(int c = start_col; c <= end_col; c++) {
        if(mode == 0 || mode == 2) { term.screen[row][c].ch = ' '; }
        if(mode == 0 || mode == 1) {
            term.screen[row][c].fg = term.current_fg;
            term.screen[row][c].bg = term.current_bg;
        }
    }
}

static void term_clear_screen(int mode) {
    if(mode == 0 || mode == 2) {
        for(int r = 0; r < TERM_ROWS; r++)
            for(int c = 0; c < TERM_COLS; c++) {
                term.screen[r][c].ch = ' ';
                term.screen[r][c].fg = term.current_fg;
                term.screen[r][c].bg = term.current_bg;
            }
    }
    if(mode == 0 || mode == 1) {
        for(int r = 0; r < term.cursor_y; r++)
            for(int c = 0; c < TERM_COLS; c++) {
                term.screen[r][c].ch = ' ';
                term.screen[r][c].fg = term.current_fg;
                term.screen[r][c].bg = term.current_bg;
            }
        for(int c = 0; c <= term.cursor_x; c++) {
            term.screen[term.cursor_y][c].ch = ' ';
            term.screen[term.cursor_y][c].fg = term.current_fg;
            term.screen[term.cursor_y][c].bg = term.current_bg;
        }
    }
    if(mode == 1 || mode == 3) {
        for(int r = term.cursor_y; r < TERM_ROWS; r++)
            for(int c = 0; c < TERM_COLS; c++) {
                term.screen[r][c].ch = ' ';
                term.screen[r][c].fg = term.current_fg;
                term.screen[r][c].bg = term.current_bg;
            }
    }
}

static int term_get_param(int idx, int def) {
    if(idx < term.ansi_param_count && term.ansi_params[idx] > 0)
        return term.ansi_params[idx];
    return def;
}

static void term_process_csi(u8 final_byte) {
    int p0 = term_get_param(0, 1);
    int p1 = term_get_param(1, 1);
    int p2 = term_get_param(2, 1);

    if(term.ansi_private) {
        switch(final_byte) {
            case 'h': // DECSET
                if(p0 == 25) term.cursor_visible = 1;
                if(p0 == 7) term.auto_wrap = 1;
                if(p0 == 6) term.origin_mode = 1;
                if(p0 == 1049) { /* switch to alt screen */ }
                if(p0 == 2004) { /* bracketed paste mode */ }
                return;
            case 'l': // DECRST
                if(p0 == 25) term.cursor_visible = 0;
                if(p0 == 7) term.auto_wrap = 0;
                if(p0 == 6) term.origin_mode = 0;
                return;
            case 'r': // DECSTBM - Set top/bottom margins
                term.scroll_top = (p0 > 0) ? p0 - 1 : 0;
                term.scroll_bottom = (p1 > 0) ? p1 - 1 : TERM_ROWS - 1;
                if(term.scroll_top >= term.scroll_bottom) term.scroll_bottom = TERM_ROWS - 1;
                term.cursor_x = 0; term.cursor_y = 0;
                return;
            case 'n': // DECDSR - Device Status Report
                if(p0 == 6) {
                    // Cursor position report - send via serial
                    char buf[32];
                    int len = 0;
                    buf[len++] = 0x1B; buf[len++] = '[';
                    // Convert cursor_y+1 to decimal
                    int val = term.cursor_y + 1;
                    if(val >= 10) buf[len++] = '0' + val / 10;
                    buf[len++] = '0' + val % 10;
                    buf[len++] = ';';
                    val = term.cursor_x + 1;
                    if(val >= 100) buf[len++] = '0' + val / 100;
                    if(val >= 10) buf[len++] = '0' + (val / 10) % 10;
                    buf[len++] = '0' + val % 10;
                    buf[len++] = 'R';
                    for(int i = 0; i < len; i++) outb(0x3F8, buf[i]);
                }
                return;
            case 's': // DECSC - Save cursor
                term.saved_cx = term.cursor_x;
                term.saved_cy = term.cursor_y;
                return;
            case 'u': // DECRC - Restore cursor
                term.cursor_x = term.saved_cx;
                term.cursor_y = term.saved_cy;
                return;
        }
        return;
    }

    switch(final_byte) {
        case 'A': // CUU - Cursor Up
            term.cursor_y -= p0;
            if(term.cursor_y < 0) term.cursor_y = 0;
            break;
        case 'B': // CUD - Cursor Down
            term.cursor_y += p0;
            if(term.cursor_y >= TERM_ROWS) term.cursor_y = TERM_ROWS - 1;
            break;
        case 'C': // CUF - Cursor Forward
            term.cursor_x += p0;
            if(term.cursor_x >= TERM_COLS) term.cursor_x = TERM_COLS - 1;
            break;
        case 'D': // CUB - Cursor Back
            term.cursor_x -= p0;
            if(term.cursor_x < 0) term.cursor_x = 0;
            break;
        case 'E': // CNL - Cursor Next Line
            term.cursor_x = 0;
            term.cursor_y += p0;
            if(term.cursor_y >= TERM_ROWS) term.cursor_y = TERM_ROWS - 1;
            break;
        case 'F': // CPL - Cursor Previous Line
            term.cursor_x = 0;
            term.cursor_y -= p0;
            if(term.cursor_y < 0) term.cursor_y = 0;
            break;
        case 'G': // CHA - Cursor Horizontal Absolute
            term.cursor_x = p0 - 1;
            if(term.cursor_x < 0) term.cursor_x = 0;
            if(term.cursor_x >= TERM_COLS) term.cursor_x = TERM_COLS - 1;
            break;
        case 'H': // CUP - Cursor Position
        case 'f':
            term.cursor_y = p0 - 1;
            term.cursor_x = p1 - 1;
            if(term.cursor_y < 0) term.cursor_y = 0;
            if(term.cursor_y >= TERM_ROWS) term.cursor_y = TERM_ROWS - 1;
            if(term.cursor_x < 0) term.cursor_x = 0;
            if(term.cursor_x >= TERM_COLS) term.cursor_x = TERM_COLS - 1;
            break;
        case 'J': // ED - Erase in Display
            term_clear_screen(p0);
            break;
        case 'K': // EL - Erase in Line
            if(p0 == 0) term_clear_line(term.cursor_y, term.cursor_x, TERM_COLS - 1, 0);
            else if(p0 == 1) term_clear_line(term.cursor_y, 0, term.cursor_x, 0);
            else if(p0 == 2) term_clear_line(term.cursor_y, 0, TERM_COLS - 1, 0);
            break;
        case 'L': // IL - Insert Lines
            for(int i = 0; i < p0; i++) term_scroll_down();
            break;
        case 'M': // DL - Delete Lines
            for(int i = 0; i < p0; i++) term_scroll_up();
            break;
        case 'P': // DCH - Delete Characters
            for(int c = term.cursor_x; c < TERM_COLS - p0; c++)
                term.screen[term.cursor_y][c] = term.screen[term.cursor_y][c + p0];
            for(int c = TERM_COLS - p0; c < TERM_COLS; c++) {
                term.screen[term.cursor_y][c].ch = ' ';
                term.screen[term.cursor_y][c].fg = term.current_fg;
                term.screen[term.cursor_y][c].bg = term.current_bg;
            }
            break;
        case '@': // ICH - Insert Characters
            for(int c = TERM_COLS - 1; c >= term.cursor_x + p0; c--)
                term.screen[term.cursor_y][c] = term.screen[term.cursor_y][c - p0];
            for(int c = term.cursor_x; c < term.cursor_x + p0; c++) {
                term.screen[term.cursor_y][c].ch = ' ';
                term.screen[term.cursor_y][c].fg = term.current_fg;
                term.screen[term.cursor_y][c].bg = term.current_bg;
            }
            break;
        case 'S': // SU - Scroll Up
            for(int i = 0; i < p0; i++) term_scroll_up();
            break;
        case 'T': // SD - Scroll Down
            for(int i = 0; i < p0; i++) term_scroll_down();
            break;
        case 'X': // ECH - Erase Characters
            for(int c = term.cursor_x; c < term.cursor_x + p0 && c < TERM_COLS; c++) {
                term.screen[term.cursor_y][c].ch = ' ';
                term.screen[term.cursor_y][c].fg = term.current_fg;
                term.screen[term.cursor_y][c].bg = term.current_bg;
            }
            break;
        case 'd': // VPA - Vertical Position Absolute
            term.cursor_y = p0 - 1;
            if(term.cursor_y < 0) term.cursor_y = 0;
            if(term.cursor_y >= TERM_ROWS) term.cursor_y = TERM_ROWS - 1;
            break;
        case 'm': // SGR - Select Graphic Rendition
            for(int i = 0; i <= term.ansi_param_count; i++) {
                int code = term.ansi_params[i];
                if(code == 0) {
                    term.current_fg = 7; term.current_bg = 0;
                    term.bold = 0; term.underline = 0; term.inverse = 0;
                    term.dim = 0; term.italic = 0;
                } else if(code == 1) term.bold = 1;
                else if(code == 2) term.dim = 1;
                else if(code == 3) term.italic = 1;
                else if(code == 4) term.underline = 1;
                else if(code == 7) term.inverse = 1;
                else if(code == 22) { term.bold = 0; term.dim = 0; }
                else if(code == 23) term.italic = 0;
                else if(code == 24) term.underline = 0;
                else if(code == 27) term.inverse = 0;
                else if(code >= 30 && code <= 37) term.current_fg = code - 30;
                else if(code == 38) {
                    if(i + 1 < term.ansi_param_count && term.ansi_params[i + 1] == 5) {
                        term.current_fg = term.ansi_params[i + 2]; i += 2;
                    } else if(i + 3 < term.ansi_param_count && term.ansi_params[i + 1] == 2) {
                        u8 r = term.ansi_params[i + 2];
                        u8 g = term.ansi_params[i + 3];
                        u8 b = term.ansi_params[i + 4]; i += 4;
                        term.current_fg = 0xFF000000 | (r << 16) | (g << 8) | b;
                    }
                }
                else if(code == 39) term.current_fg = 7;
                else if(code >= 40 && code <= 47) term.current_bg = code - 40;
                else if(code == 48) {
                    if(i + 1 < term.ansi_param_count && term.ansi_params[i + 1] == 5) {
                        term.current_bg = term.ansi_params[i + 2]; i += 2;
                    } else if(i + 3 < term.ansi_param_count && term.ansi_params[i + 1] == 2) {
                        u8 r = term.ansi_params[i + 2];
                        u8 g = term.ansi_params[i + 3];
                        u8 b = term.ansi_params[i + 4]; i += 4;
                        term.current_bg = 0xFF000000 | (r << 16) | (g << 8) | b;
                    }
                }
                else if(code == 49) term.current_bg = 0;
                else if(code >= 90 && code <= 97) term.current_fg = code - 90 + 8;
                else if(code >= 100 && code <= 107) term.current_bg = code - 100 + 8;
            }
            break;
        case 'n': // DSR - Device Status Report
            if(p0 == 6) {
                // Cursor position report
                char buf[32]; int len = 0;
                buf[len++] = 0x1B; buf[len++] = '[';
                int val = term.cursor_y + 1;
                if(val >= 10) buf[len++] = '0' + val / 10;
                buf[len++] = '0' + val % 10;
                buf[len++] = ';';
                val = term.cursor_x + 1;
                if(val >= 100) buf[len++] = '0' + val / 100;
                if(val >= 10) buf[len++] = '0' + (val / 10) % 10;
                buf[len++] = '0' + val % 10;
                buf[len++] = 'R';
                for(int i = 0; i < len; i++) outb(0x3F8, buf[i]);
            } else if(p0 == 5) {
                outb(0x3F8, 0x1B); outb(0x3F8, '['); outb(0x3F8, '0'); outb(0x3F8, 'n');
            }
            break;
        case 's': // SCP - Save Cursor Position
            term.saved_cx = term.cursor_x;
            term.saved_cy = term.cursor_y;
            break;
        case 'u': // RCP - Restore Cursor Position
            term.cursor_x = term.saved_cx;
            term.cursor_y = term.saved_cy;
            break;
        case 'r': // DECSTBM
            term.scroll_top = (p0 > 0) ? p0 - 1 : 0;
            term.scroll_bottom = (p1 > 0) ? p1 - 1 : TERM_ROWS - 1;
            if(term.scroll_top >= term.scroll_bottom) term.scroll_bottom = TERM_ROWS - 1;
            term.cursor_x = 0; term.cursor_y = 0;
            break;
        case 'h': // SM - Set Mode
            if(p0 == 4) term.insert_mode = 1;
            if(p0 == 20) term.auto_wrap = 1;
            break;
        case 'l': // RM - Reset Mode
            if(p0 == 4) term.insert_mode = 0;
            if(p0 == 20) term.auto_wrap = 0;
            break;
        case 'I': // CHT - Cursor Horizontal Tab
            term.cursor_x = (term.cursor_x + term.tab_width) / term.tab_width * term.tab_width;
            if(term.cursor_x >= TERM_COLS) term.cursor_x = TERM_COLS - 1;
            break;
        case 'Z': // CBT - Cursor Backward Tab
            term.cursor_x = (term.cursor_x / term.tab_width) * term.tab_width - term.tab_width;
            if(term.cursor_x < 0) term.cursor_x = 0;
            break;
    }
}

static void term_parse_byte(u8 byte) {
    switch(term.ansi_state) {
        case 0: // Normal
            if(byte == 0x1B) {
                term.ansi_state = 1;
            } else {
                term_put_char(byte);
            }
            break;
        case 1: // ESC received
            if(byte == '[') {
                term.ansi_state = 2;
                term.ansi_param_count = 0;
                term.ansi_private = 0;
                for(int i = 0; i < 16; i++) term.ansi_params[i] = 0;
            } else if(byte == ']') {
                term.ansi_state = 4; // OSC sequence
            } else if(byte == 'M') {
                // Reverse Index (move cursor up, scroll if at top)
                if(term.cursor_y <= term.scroll_top) term_scroll_down();
                else term.cursor_y--;
            } else if(byte == '7') {
                term.saved_cx = term.cursor_x;
                term.saved_cy = term.cursor_y;
                term.ansi_state = 0;
            } else if(byte == '8') {
                term.cursor_x = term.saved_cx;
                term.cursor_y = term.saved_cy;
                term.ansi_state = 0;
            } else if(byte == 'D') {
                // Index (move cursor down, scroll if at bottom)
                if(term.cursor_y >= term.scroll_bottom) term_scroll_up();
                else term.cursor_y++;
                term.ansi_state = 0;
            } else if(byte == 'c') {
                // RIS - Full reset
                term_init();
                term.ansi_state = 0;
            } else {
                term.ansi_state = 0;
            }
            break;
        case 2: // CSI sequence
            if(byte == '?') {
                term.ansi_private = 1;
            } else if(byte >= '0' && byte <= '9') {
                term.ansi_params[term.ansi_param_count] =
                    term.ansi_params[term.ansi_param_count] * 10 + (byte - '0');
            } else if(byte == ';') {
                if(term.ansi_param_count < 15) term.ansi_param_count++;
            } else if(byte == ' ' || byte == '>' || byte == '<' || byte == '=') {
                // Ignore intermediate bytes
            } else {
                // Final byte
                term_process_csi(byte);
                term.ansi_state = 0;
            }
            break;
        case 4: // OSC sequence (Operating System Command)
            // Skip until ST (ESC \) or BEL
            if(byte == 0x07 || byte == '\\') term.ansi_state = 0;
            break;
    }
}

// --- Render terminal to LFB ---
static void term_render(int screen_x, int screen_y) {
    for(int row = 0; row < TERM_ROWS; row++) {
        for(int col = 0; col < TERM_COLS; col++) {
            term_cell_t* cell = &term.screen[row][col];
            u32 bg = 0xFF000000;
            if(cell->bg < 16) bg = term_palette[cell->bg & 0x0F];
            else if(cell->bg < 256) bg = term_palette256[cell->bg];
            else bg = cell->bg; // Direct ARGB

            u32 fg = 0xFFFFFFFF;
            if(cell->fg < 16) fg = term_palette[cell->fg & 0x0F];
            else if(cell->fg < 256) fg = term_palette256[cell->fg];
            else fg = cell->fg; // Direct ARGB

            // Underline
            if(cell->ch != ' ' && term.underline) {
                // Draw underline on last row of cell
            }

            int px = screen_x + col * TERM_FONT_W;
            int py = screen_y + row * TERM_FONT_H;

            // Draw background
            for(int y = 0; y < TERM_FONT_H; y++)
                for(int x = 0; x < TERM_FONT_W; x++)
                    put_pixel(px + x, py + y, bg);

            // Draw character
            if(cell->ch >= 32 && cell->ch < 127) {
                draw_char(px, py, cell->ch, fg);
            } else if(cell->ch >= 0xC0) {
                // Cyrillic: draw as inverse '?' placeholder
                draw_char(px, py, '?', fg);
            }
        }
    }

    // Draw cursor
    if(term.cursor_visible && term.cursor_blink_state) {
        int cx = screen_x + term.cursor_x * TERM_FONT_W;
        int cy = screen_y + term.cursor_y * TERM_FONT_H;
        for(int y = TERM_FONT_H - 2; y < TERM_FONT_H; y++)
            for(int x = 0; x < TERM_FONT_W; x++)
                put_pixel(cx + x, cy + y, 0xFFFFFFFF);
    }
}

// --- Scroll the terminal view ---
static void term_scroll_view(int dir) {
    term.scroll_pos += dir;
    if(term.scroll_pos < 0) term.scroll_pos = 0;
    if(term.scroll_pos > term.scroll_count) term.scroll_pos = term.scroll_count;
}

// --- Terminal tab: Serial I/O + Keyboard input ---
static void term_input_loop() {
    int screen_x = 10;
    int screen_y = 62;
    int term_w = TERM_COLS * TERM_FONT_W;  // 640
    int term_h = TERM_ROWS * TERM_FONT_H;  // 400

    // Print welcome message into the terminal
    const char* welcome[] = {
        "\x1b[1;36m╔══════════════════════════════════════════════════════════════╗\x1b[0m",
        "\x1b[1;36m║\x1b[0m  \x1b[1;33mTerminal v1.0\x1b[0m — VT100 Terminal Emulator                   \x1b[1;36m║\x1b[0m",
        "\x1b[1;36m║\x1b[0m  Serial I/O: COM1 @ 115200 baud, 8N1                        \x1b[1;36m║\x1b[0m",
        "\x1b[1;36m║\x1b[0m                                                              \x1b[1;36m║\x1b[0m",
        "\x1b[1;36m║\x1b[0m  \x1b[1;32mFeatures:\x1b[0m                                                 \x1b[1;36m║\x1b[0m",
        "\x1b[1;36m║\x1b[0m    • ANSI/VT100 escape sequences (CSI, OSC, SGR)            \x1b[1;36m║\x1b[0m",
        "\x1b[1;36m║\x1b[0m    • 256-color palette (16 standard + 216 RGB + 24 gray)     \x1b[1;36m║\x1b[0m",
        "\x1b[1;36m║\x1b[0m    • 2000-line scrollback buffer (PgUp/PgDn)                \x1b[1;36m║\x1b[0m",
        "\x1b[1;36m║\x1b[0m    • Full cursor control (CUU/CUD/CUF/CUB/CUP/ED/EL)       \x1b[1;36m║\x1b[0m",
        "\x1b[1;36m║\x1b[0m    • Insert/Delete lines/characters                          \x1b[1;36m║\x1b[0m",
        "\x1b[1;36m║\x1b[0m    • DEC Private Mode (Origin, Auto-wrap, Cursor visible)   \x1b[1;36m║\x1b[0m",
        "\x1b[1;36m║\x1b[0m    • Tab stops, Bell, Reverse Index, Soft reset              \x1b[1;36m║\x1b[0m",
        "\x1b[1;36m║\x1b[0m                                                              \x1b[1;36m║\x1b[0m",
        "\x1b[1;36m║\x1b[0m  \x1b[1;31mESC\x1b[0m = Exit to BIOS Setup                                  \x1b[1;36m║\x1b[0m",
        "\x1b[1;36m╚══════════════════════════════════════════════════════════════╝\x1b[0m",
        "",
        "\x1b[1;37m$ \x1b[0m",
    };
    for(int i = 0; i < 17; i++) {
        const char* s = welcome[i];
        while(*s) { term_parse_byte(*s); s++; }
    }

    // Send initial DSR (cursor position report request)
    outb(0x3F8, 0x1B); outb(0x3F8, '['); outb(0x3F8, '6'); outb(0x3F8, 'n');

    while(1) {
        // Read from serial port (COM1 = 0x3F8)
        int serial_timeout = 100;
        while(serial_timeout--) {
            if(inb(0x3FD) & 0x01) { // LSR: Data Ready
                u8 c = inb(0x3F8);
                term_parse_byte(c);
            } else {
                break;
            }
        }

        // Read keyboard
        u8 key = kbd_read_scancode();
        if(key) {
            if(key == 0xE0) {
                while(!(key = kbd_read_scancode()));
            }

            // Track shift and ctrl before dropping break codes
            static int shift_held = 0;
            static int ctrl_held = 0;
            if(key == 0x2A || key == 0x36) { shift_held = 1; continue; } // L/R Shift make
            if(key == 0xAA || key == 0xB6) { shift_held = 0; continue; } // L/R Shift break
            if(key == 0x1D) { ctrl_held = 1; continue; } // L Ctrl make
            if(key == 0x9D) { ctrl_held = 0; continue; } // L Ctrl break

            if(key & 0x80) continue;

            // ESC = exit terminal
            if(key == 0x01) return;

            // Scrolled view: scroll up/down
            if(key == 0x49) { term_scroll_view(5); } // Page Up
            if(key == 0x51) { term_scroll_view(-5); } // Page Down

            // Arrow keys
            if(key == 0x48) { outb(0x3F8, 0x1B); outb(0x3F8, '['); outb(0x3F8, 'A'); }
            if(key == 0x50) { outb(0x3F8, 0x1B); outb(0x3F8, '['); outb(0x3F8, 'B'); }
            if(key == 0x4D) { outb(0x3F8, 0x1B); outb(0x3F8, '['); outb(0x3F8, 'C'); }
            if(key == 0x4B) { outb(0x3F8, 0x1B); outb(0x3F8, '['); outb(0x3F8, 'D'); }

            // Enter
            if(key == 0x1C) { outb(0x3F8, '\r'); }

            // Backspace
            if(key == 0x0E) { outb(0x3F8, 0x7F); }

            // Tab
            if(key == 0x0F) { outb(0x3F8, '\t'); }

            // Ctrl+C (scancode for C is 0x2E)
            if(ctrl_held && key == 0x2E) { outb(0x3F8, 0x03); continue; }

            // Ctrl+L (clear screen) (scancode for L is 0x26)
            if(ctrl_held && key == 0x26) { outb(0x3F8, 0x0C); continue; }

            // Regular ASCII keys - use full PS/2 scancode set 1 table
            {

                // Complete scancode set 1 -> ASCII table (unshifted)
                static const char sc1_to_ascii[128] = {
                    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', 8, 9,
                    'q','w','e','r','t','y','u','i','o','p','[',']',13, 0,'a','s',
                    'd','f','g','h','j','k','l',';',39,'`', 0, 92,'z','x','c','v',
                    'b','n','m',',','.','/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
                    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '-', 0, 0, 0, '+', 0,
                    0, 0, 0, '.', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
                };
                // Shifted symbols row
                static const char sc1_shifted[128] = {
                    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+', 8, 9,
                    'Q','W','E','R','T','Y','U','I','O','P','{','}',13, 0,'A','S',
                    'D','F','G','H','J','K','L',':','"','~', 0, '|','Z','X','C','V',
                    'B','N','M','<','>','?', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
                    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '-', 0, 0, 0, '+', 0,
                    0, 0, 0, '.', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
                };

                if(key < 128) {
                    char c;
                    if(shift_held) c = sc1_shifted[key];
                    else c = sc1_to_ascii[key];
                    if(c) outb(0x3F8, c);
                }
            }
        }

        // Render
        term_render(screen_x, screen_y);

        // Draw border + info
        fill_rect(screen_x - 1, screen_y - 1, term_w + 2, 1, 0xFF00AAAA);
        fill_rect(screen_x - 1, screen_y + term_h, term_w + 2, 1, 0xFF00AAAA);
        fill_rect(screen_x - 1, screen_y, 1, term_h, 0xFF00AAAA);
        fill_rect(screen_x + term_w, screen_y, 1, term_h, 0xFF00AAAA);

        // Info bar
        fill_rect(screen_x, screen_y + term_h + 2, term_w, 14, 0xFF0000AA);
        draw_string(screen_x + 4, screen_y + term_h + 3, "Terminal v1.0 | ESC=Exit | PgUp/PgDn=Scroll | Type to send", 0xFFFFFFFF);

        // Blink cursor every ~500ms
        static int blink_counter = 0;
        blink_counter++;
        if(blink_counter > 30) {
            term.cursor_blink_state = !term.cursor_blink_state;
            blink_counter = 0;
        }

        for(volatile int d = 0; d < 2000000; d++);
    }
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
    
    // Explicitly restore IDE controller to Drive 0 (Master) to prevent booting hangs
    // due to early returns in ata_identify skipping the restore.
    outb(0x1F6, 0xA0);
    for(volatile int j=0; j<1000; j++);
    
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
    draw_string(0, log_y, "Initializing USB Controllers .. Done.", 0xFFFFFFFF); log_y += 16;
    draw_string(0, log_y, uuid_msg, 0xFFFFFFFF); log_y += 16;

    // Boot Summary: Boot Order
    draw_string(0, log_y, "Boot Order: ", 0xFF00FFFF);
    int bx = 96;
    for(int i = 0; i < boot_order_count; i++) {
        const char* bname = boot_device_name(boot_order[i]);
        while(*bname) {
            draw_char(bx, log_y, *bname++, 0xFFFFFFFF);
            bx += 8;
        }
        if(i < boot_order_count - 1) {
            draw_char(bx, log_y, ' ', 0xFF888888);
            bx += 8;
            draw_char(bx, log_y, '>', 0xFF00FFFF);
            bx += 8;
            draw_char(bx, log_y, ' ', 0xFF888888);
            bx += 8;
        }
    }
    log_y += 24;

    // PCI Device Summary
    draw_string(0, log_y, "PCI Devices:", 0xFF00FFFF); log_y += 16;
    for(int i = 0; i < 8; i++) {
        pci_device_t* pdev = pci_find_device_by_index(i);
        if(!pdev) break;
        const char* pname = get_pci_device_name(pdev->vendor, pdev->device);
        const char* cname = get_pci_class_name(pdev->class_code);
        char pcibuf[80];
        int pp = 0;
        // BDF
        const char* hex = "0123456789ABCDEF";
        pcibuf[pp++] = ' ';
        pcibuf[pp++] = ' ';
        pcibuf[pp++] = hex[(pdev->bdf >> 8) & 0xF]; pcibuf[pp++] = ':';
        pcibuf[pp++] = hex[(pdev->bdf >> 3) & 0xF]; pcibuf[pp++] = hex[pdev->bdf & 0xF]; pcibuf[pp++] = '.';
        pcibuf[pp++] = hex[pdev->bdf & 0x7]; pcibuf[pp++] = ' ';
        // Class
        while(*cname) pcibuf[pp++] = *cname++;
        pcibuf[pp++] = ':';
        pcibuf[pp++] = ' ';
        // Name
        while(*pname && pp < 75) pcibuf[pp++] = *pname++;
        pcibuf[pp] = 0;
        draw_string(0, log_y, pcibuf, 0xFFFFFFFF);
        log_y += 14;
    }

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
        // --- BOOT PASSWORD CHECK ---
        if(password_enabled) {
            fill_rect(0, 0, 800, 600, 0xFF0000AA);
            draw_string(280, 200, "BIOS SETUP PASSWORD REQUIRED", 0xFFFFFF00);
            draw_string(300, 240, "Enter password:", 0xFFFFFFFF);

            char pass_buf[32];
            int pass_len = 0;
            pass_buf[0] = 0;

            while(1) {
                // Draw asterisks
                fill_rect(300, 270, 200, 20, 0xFF000088);
                for(int a = 0; a < pass_len; a++) draw_char(310 + a * 12, 274, '*', 0xFFFFFF00);

                u8 sc = kbd_read_scancode();
                if(!sc) continue;
                if(sc == 0xE0) { while(!(sc = kbd_read_scancode())); }
                if(sc & 0x80) continue;

                if(sc == 0x1C) { // Enter
                    if(check_password(pass_buf)) break;
                    draw_string(280, 310, "WRONG PASSWORD! Try again.", 0xFFFF0000);
                    for(volatile int d = 0; d < 50000000; d++);
                    fill_rect(280, 310, 300, 16, 0xFF0000AA);
                    pass_len = 0; pass_buf[0] = 0;
                } else if(sc == 0x0E) { // Backspace
                    if(pass_len > 0) pass_len--;
                    pass_buf[pass_len] = 0;
                } else if(sc == 0x01) { // ESC - cancel
                    show_setup = 0;
                    goto skip_setup;
                } else {
                    // Simple scancode to ASCII
                    const char* scmap = "\0\x1B""1234567890-=\tqwertyuiop[]\\asdfghjkl;'`"
                                        "\0\\zxcvbnm,./\0*\0 \0";
                    if(sc < 57 && scmap[sc]) {
                        pass_buf[pass_len++] = scmap[sc];
                        pass_buf[pass_len] = 0;
                    }
                }
            }
            // Password accepted!
            fill_rect(0, 0, 800, 600, 0xFF0000AA);
            draw_string(300, 280, "ACCESS GRANTED", 0xFF00FF00);
            for(volatile int d = 0; d < 20000000; d++);
        }
skip_setup:;

        int backup_ide = ide_en;
        int backup_acpi = acpi_en;
        int backup_usb = usb_en;
        const u8* backup_font = current_font;
        int backup_boot_order[4];
        for(int i = 0; i < 4; i++) backup_boot_order[i] = boot_order[i];
        int backup_splash = splash_enabled;
        int backup_passwd_en = password_enabled;
        
        int current_tab = 0; // 0=Main, 1=Advanced, 2=Boot, 3=Monitor, 4=Security, 5=Exit
        int selected_item = 0;

redraw_setup:
        restore_mouse_bg();
        fill_rect(0, 0, 800, 600, COLOR_BG);
        fill_rect(0, 0, 800, 32, COLOR_TOP_BAR);
        draw_string(308, 8, "VibeBIOS", COLOR_TEXT_NORM);
        draw_string(700, 8, "Rev. 4.0", COLOR_TEXT_NORM);

        // Tab bar (6 tabs)
        int tabs_x[] = {10, 80, 155, 240, 330, 425};
        char* tabs[] = {"Main", "Advanced", "Boot", "Monitor", "Security", "Exit"};
        fill_rect(0, 32, 800, 24, COLOR_BG);
        for(int i=0; i<6; i++) {
            if(i == current_tab) {
                fill_rect(tabs_x[i]-5, 32, 78, 24, COLOR_SEL_BG);
                draw_string(tabs_x[i], 36, tabs[i], COLOR_TEXT_SEL);
            } else {
                draw_string(tabs_x[i], 36, tabs[i], COLOR_TEXT_SEL);
            }
        }

        // Borders + Bottom bar (static)
        fill_rect(10, 60, 780, 2, COLOR_PANEL);
        fill_rect(500, 60, 2, 480, COLOR_PANEL);
        fill_rect(10, 540, 780, 2, COLOR_PANEL);
        fill_rect(0, 560, 800, 40, COLOR_BOT_BAR);
        draw_string(10, 572, "F1 Help  ^v Select  F5/F6 Change  F9 Defaults  F10 Save", COLOR_TEXT_NORM);
        goto redraw_content;

redraw_content:
        // Only redraw the content area (y=62 to y=538, full width between borders)
        fill_rect(12, 62, 776, 476, COLOR_BG);

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
            // If year is garbage (QEMU default), fix it
            if(yr < 24 || yr > 99) yr = 26; // Default to 2026
            if(mo < 1 || mo > 12) mo = 1;
            int max_days = 31;
            if(mo == 4 || mo == 6 || mo == 9 || mo == 11) max_days = 30;
            else if(mo == 2) {
                // Leap year: divisible by 4, not by 100, or by 400
                int full_yr = 2000 + yr;
                if((full_yr % 4 == 0 && full_yr % 100 != 0) || (full_yr % 400 == 0)) max_days = 29;
                else max_days = 28;
            }
            if(day < 1 || day > max_days) day = 1;
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
            // --- BOOT TAB: Boot Order Configuration ---
            draw_string(520, 80, "Boot Priority Order", COLOR_TEXT_SEL);
            draw_string(20, 80, "Boot Device Priority", COLOR_TEXT_SEL);
            draw_string(20, 100, "Use F5/F6 or +/- to change order", 0xFF888888);

            for(int i = 0; i < boot_order_count; i++) {
                char buf[40];
                int p = 0;
                buf[p++] = '0' + (i + 1); buf[p++] = '.'; buf[p++] = ' '; buf[p++] = ' ';
                const char* name = boot_device_name(boot_order[i]);
                while(*name) buf[p++] = *name++;
                buf[p] = 0;

                u32 color = (selected_item == i) ? 101 : COLOR_TEXT_SEL;
                if(selected_item == i) {
                    fill_rect(38, 118 + i * 22, 440, 20, 0xFF0000AA);
                }
                draw_string(40, 120 + i * 22, buf, color);
            }

            draw_string(20, 220, "Boot Splash Screen", COLOR_TEXT_SEL);
            draw_string(250, 220, splash_enabled ? "[Enabled]" : "[Disabled]", (selected_item==4) ? 101 : COLOR_TEXT_SEL);
        }
        else if(current_tab == 3) {
            // --- MONITOR TAB: Hardware Information ---
            cache_cpuid();
            draw_string(520, 80, "Hardware Monitor", COLOR_TEXT_SEL);

            // CPU Info
            draw_string(20, 80, "CPU Information", 0xFF00FFFF);
            draw_string(40, 100, "Processor:", COLOR_TEXT_SEL);
            draw_string(200, 100, cpu_name_cached, COLOR_TEXT_SEL);

            char buf[40];
            int p;
            // CPU Vendor
            u32 eax, ebx, ecx, edx;
            asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "0"(0));
            p = 0;
            buf[p++] = (ebx & 0xFF); buf[p++] = (ebx >> 8) & 0xFF;
            buf[p++] = (ebx >> 16) & 0xFF; buf[p++] = (ebx >> 24) & 0xFF;
            buf[p++] = (edx & 0xFF); buf[p++] = (edx >> 8) & 0xFF;
            buf[p++] = (edx >> 16) & 0xFF; buf[p++] = (edx >> 24) & 0xFF;
            buf[p++] = (ecx & 0xFF); buf[p++] = (ecx >> 8) & 0xFF;
            buf[p++] = (ecx >> 16) & 0xFF; buf[p++] = (ecx >> 24) & 0xFF;
            buf[p] = 0;
            draw_string(40, 120, "Vendor:", COLOR_TEXT_SEL);
            draw_string(200, 120, buf, COLOR_TEXT_SEL);

            // CPUID features
            draw_string(40, 140, "Features:", COLOR_TEXT_SEL);
            p = 0;
            if(cpu_features_edx & (1 << 4)) { buf[p++] = 'T'; buf[p++] = 'S'; buf[p++] = ' '; }
            if(cpu_features_edx & (1 << 5)) { buf[p++] = 'V'; buf[p++] = 'M'; buf[p++] = 'X'; buf[p++] = ' '; }
            if(cpu_features_edx & (1 << 8)) { buf[p++] = 'C'; buf[p++] = 'M'; buf[p++] = 'P'; buf[p++] = ' '; }
            if(cpu_features_edx & (1 << 15)) { buf[p++] = 'C'; buf[p++] = 'M'; buf[p++] = 'O'; buf[p++] = 'V'; buf[p++] = ' '; }
            if(cpu_features_edx & (1 << 23)) { buf[p++] = 'M'; buf[p++] = 'M'; buf[p++] = 'X'; buf[p++] = ' '; }
            if(cpu_features_edx & (1 << 25)) { buf[p++] = 'S'; buf[p++] = 'S'; buf[p++] = 'E'; buf[p++] = ' '; }
            if(cpu_features_edx & (1 << 26)) { buf[p++] = 'S'; buf[p++] = 'S'; buf[p++] = 'E'; buf[p++] = '2'; buf[p++] = ' '; }
            if(cpu_features_ecx & (1 << 0)) { buf[p++] = 'S'; buf[p++] = 'S'; buf[p++] = 'E'; buf[p++] = '3'; buf[p++] = ' '; }
            if(cpu_features_ecx & (1 << 9)) { buf[p++] = 'S'; buf[p++] = 'S'; buf[p++] = 'E'; buf[p++] = '4'; buf[p++] = '2'; buf[p++] = ' '; }
            if(cpu_features_ecx & (1 << 19)) { buf[p++] = 'S'; buf[p++] = 'S'; buf[p++] = 'E'; buf[p++] = '4'; buf[p++] = '.'; buf[p++] = '1'; buf[p++] = ' '; }
            if(cpu_features_ecx & (1 << 28)) { buf[p++] = 'A'; buf[p++] = 'V'; buf[p++] = 'X'; buf[p++] = ' '; }
            if(p == 0) { buf[p++] = 'N'; buf[p++] = 'o'; buf[p++] = 'n'; buf[p++] = 'e'; }
            buf[p] = 0;
            draw_string(200, 140, buf, COLOR_TEXT_SEL);

            // RAM
            draw_string(20, 170, "Memory Information", 0xFF00FFFF);
            draw_string(40, 190, "Base Memory:", COLOR_TEXT_SEL);
            draw_string(250, 190, "640 KB", COLOR_TEXT_SEL);
            draw_string(40, 210, "Extended Memory:", COLOR_TEXT_SEL);
            u32 ram = get_ram_kb();
            u32_to_str(ram, buf);
            int bp = 0; while(buf[bp]) bp++;
            buf[bp++] = ' '; buf[bp++] = 'K'; buf[bp++] = 'B'; buf[bp] = 0;
            draw_string(250, 210, buf, COLOR_TEXT_SEL);

            // E820 Map
            draw_string(20, 240, "Memory Map (E820)", 0xFF00FFFF);
            draw_string(40, 260, "0x00000000 - 0x0009FFFF : Usable (640KB)", 0xFF00FF00);
            draw_string(40, 280, "0x00100000 - Extended  : Usable (RAM)", 0xFF00FF00);

            // RTC
            draw_string(20, 310, "RTC / CMOS", 0xFF00FFFF);
            u8 h = bcd_to_bin(rtc_read(0x04));
            u8 m = bcd_to_bin(rtc_read(0x02));
            u8 s = bcd_to_bin(rtc_read(0x00));
            p = 0;
            buf[p++] = '0'+h/10; buf[p++] = '0'+h%10; buf[p++]=':';
            buf[p++] = '0'+m/10; buf[p++] = '0'+m%10; buf[p++]=':';
            buf[p++] = '0'+s/10; buf[p++] = '0'+s%10; buf[p] = 0;
            draw_string(40, 330, "Time:", COLOR_TEXT_SEL);
            draw_string(200, 330, buf, COLOR_TEXT_SEL);
        }
        else if(current_tab == 4) {
            // --- SECURITY TAB ---
            draw_string(520, 80, "Security Settings", COLOR_TEXT_SEL);
            draw_string(20, 80, "Boot Password", COLOR_TEXT_SEL);
            draw_string(250, 80, password_enabled ? "[Enabled]" : "[Disabled]", (selected_item==0) ? 101 : COLOR_TEXT_SEL);

            if(password_enabled) {
                draw_string(20, 100, "Password Status:", COLOR_TEXT_SEL);
                draw_string(250, 100, "Set (hashed)", 0xFF00FF00);
            } else {
                draw_string(20, 100, "Password Status:", COLOR_TEXT_SEL);
                draw_string(250, 100, "Not Set", 0xFF888888);
            }

            draw_string(20, 140, "About", 0xFF00FFFF);
            draw_string(40, 160, "VibeBIOS v4.0 - Custom BIOS Firmware", COLOR_TEXT_SEL);
            draw_string(40, 180, "Developed with passion and assembly", COLOR_TEXT_SEL);
            draw_string(40, 200, "(and a lot of QEMU debugging)", COLOR_TEXT_SEL);
        }
        else if(current_tab == 5) {
            // --- EXIT TAB ---
            draw_string(520, 80, "Exit Setup", COLOR_TEXT_SEL);
            draw_string(20, 80, "Exit Saving Changes", (selected_item==0) ? 101 : COLOR_TEXT_SEL);
            draw_string(20, 100, "Exit Discarding Changes", (selected_item==1) ? 101 : COLOR_TEXT_SEL);
            draw_string(20, 120, "Load Setup Defaults", (selected_item==2) ? 101 : COLOR_TEXT_SEL);
        }
        
        save_mouse_bg();
        draw_mouse_cursor();

        while(1) {
            // Wait a little bit to prevent ultra-fast polling and blinking
            for(volatile int j=0; j<2000000; j++);

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
                        if (mouse_x >= tabs_x[0] && mouse_x <= tabs_x[0]+70) current_tab = 0;
                        else if (mouse_x >= tabs_x[1] && mouse_x <= tabs_x[1]+70) current_tab = 1;
                        else if (mouse_x >= tabs_x[2] && mouse_x <= tabs_x[2]+70) current_tab = 2;
                        else if (mouse_x >= tabs_x[3] && mouse_x <= tabs_x[3]+70) current_tab = 3;
                        else if (mouse_x >= tabs_x[4] && mouse_x <= tabs_x[4]+70) current_tab = 4;
                        else if (mouse_x >= tabs_x[5] && mouse_x <= tabs_x[5]+70) current_tab = 5;
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
                        goto redraw_content;
                    }
                    if(current_tab == 1) {
                        if(mouse_y >= 80 && mouse_y <= 96) selected_item = 0;
                        if(mouse_y >= 100 && mouse_y <= 116) selected_item = 1;
                        if(mouse_y >= 120 && mouse_y <= 136) selected_item = 2;

                        if(old_sel == selected_item && mouse_x >= 20) {
                            key = 0x1C; // Simulate Enter press to open popup
                            goto process_key;
                        }
                        goto redraw_content;
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
                current_tab = (current_tab + 1) % 6;
                selected_item = 0;
                goto redraw_setup;
            }
            if(key == 0x4B) { // Left Arrow
                current_tab = (current_tab + 5) % 6;
                selected_item = 0;
                goto redraw_setup;
            }
            if(key == 0x50) { // Down Arrow
                selected_item++;
                goto redraw_content;
            }
            if(key == 0x48) { // Up Arrow
                if(selected_item > 0) selected_item--;
                goto redraw_content;
            }
            
            // Mouse handling moved to the top of the loop!

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
                        u8 mo = bcd_to_bin(rtc_read(0x08));
                        u8 yr = bcd_to_bin(rtc_read(0x09));
                        int max_days = 31;
                        if(mo == 4 || mo == 6 || mo == 9 || mo == 11) max_days = 30;
                        else if(mo == 2) {
                            int full_yr = 2000 + yr;
                            if((full_yr % 4 == 0 && full_yr % 100 != 0) || (full_yr % 400 == 0)) max_days = 29;
                            else max_days = 28;
                        }
                        d = d + dir;
                        if(d < 1) d = max_days;
                        if(d > max_days) d = 1;
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
                if(current_tab == 2) {
                    // Boot Order: F5/F6 to change selected item's device
                    if(selected_item >= 0 && selected_item < boot_order_count) {
                        if(dir == 1) {
                            boot_order[selected_item] = (boot_order[selected_item] + 1) % 4;
                        } else {
                            boot_order[selected_item] = (boot_order[selected_item] + 3) % 4;
                        }
                    }
                    if(selected_item == 4) {
                        // Splash toggle
                        splash_enabled = splash_enabled ? 0 : 1;
                    }
                }
                if(current_tab == 4) {
                    // Security: Password toggle
                    if(selected_item == 0) {
                        const char* opts2[] = {"Disabled", "Enabled"};
                        int res = show_popup_menu("Boot Password", opts2, 2, password_enabled);
                        if(res != -1) {
                            password_enabled = res;
                            if(res == 1) {
                                // Set password - simple: hash a default for now
                                // In real BIOS you'd prompt for input
                                hash_password("vibebios", password_hash);
                            }
                        }
                    }
                }
                goto redraw_content;
            }

            if(current_tab == 0 && selected_item == 2) { // Direct Font selection
                if(key == 0x02) { current_font = font_ibm8x16; goto redraw_content; }
                if(key == 0x03) { current_font = font_fixedsys8x16; goto redraw_content; }
                if(key == 0x04) { current_font = font_sun8x16; goto redraw_content; }
            }
            
            if(key == 0x01 || (key == 0x1C && current_tab == 5 && selected_item == 1)) { // ESC -> Exit
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
                            for(int i = 0; i < 4; i++) boot_order[i] = backup_boot_order[i];
                            splash_enabled = backup_splash;
                            password_enabled = backup_passwd_en;
                            goto main_loop;
                        }
                        else goto redraw_setup;
                    }
                    if(dkey == 0x01) goto redraw_setup;
                    for(volatile int j=0; j<2000000; j++);
                }
            }
            if(key == 0x44 || (key == 0x1C && current_tab == 5 && selected_item == 0)) { // F10 -> Save and Exit
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
            
        }
    }

    if(show_menu) {
        fill_rect(0, 0, 800, 600, 0xFF0000AA); 
        draw_string(280, 100, "VIBEBIOS BOOT MENU", 0xFF00FFFF);
        draw_string(250, 150, "1. ATA HARD DRIVE 0", 0xFFFFFFFF);
        draw_string(250, 180, "2. CD-ROM DRIVE", 0xFFFFFFFF);
        draw_string(250, 210, "3. FLOPPY DRIVE 0", 0xFFFFFFFF);
        draw_string(250, 240, "4. NETWORK BOOT (PXE)", 0xFFFFFFFF);
        draw_string(250, 290, "Select device (1-4):", 0xFFFFFF00);

        while(1) {
            u8 key = kbd_read_scancode();
            if(key) {
                if(key == 0x02 || key == 0x4F) { 
                    boot_device = 0x80;
                    break;
                } else if(key == 0x03 || key == 0x50) { 
                    boot_device = 0xE0;
                    break;
                } else if(key == 0x04 || key == 0x51) { 
                    boot_device = 0x00;
                    break;
                } else if(key == 0x05 || key == 0x52) { 
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
