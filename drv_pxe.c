#include "drv_pxe.h"
#include "io.h"

typedef unsigned char u8;
typedef unsigned int u32;

static int net_bus = -1;
static int net_dev = -1;
static int net_func = -1;

int pxe_is_ready() {
    for(int bus = 0; bus < 1; bus++) {
        for(int dev = 0; dev < 32; dev++) {
            for(int func = 0; func < 8; func++) {
                u32 addr = 0x80000000 | (bus << 16) | (dev << 11) | (func << 8) | 0x08;
                outl(0xCF8, addr);
                u32 class_info = inl(0xCFC);
                if((class_info >> 24) == 0x02) { // Network controller
                    net_bus = bus;
                    net_dev = dev;
                    net_func = func;
                    return 1;
                }
            }
        }
    }
    return 0;
}

int pxe_boot() {
    if (net_bus == -1) {
        if (!pxe_is_ready()) return 0;
    }

    // Enable memory space
    u32 cmd_addr = 0x80000000 | (net_bus << 16) | (net_dev << 11) | (net_func << 8) | 0x04;
    outl(0xCF8, cmd_addr);
    u32 orig_cmd = inl(0xCFC);
    outl(0xCF8, cmd_addr);
    outl(0xCFC, orig_cmd | 2);

    // Read ROM BAR
    u32 pci_addr = 0x80000000 | (net_bus << 16) | (net_dev << 11) | (net_func << 8) | 0x30;
    outl(0xCF8, pci_addr);
    u32 orig_bar = inl(0xCFC);

    // Map to 0xFE000000
    outl(0xCF8, pci_addr);
    outl(0xCFC, 0xFE000000 | 1);

    // Copy ROM
    u8* rom = (u8*)0xFE000000;
    if (rom[0] == 0x55 && rom[1] == 0xAA) {
        u32 size = rom[2] * 512;
        u8* dest = (u8*)0xC8000;
        for (u32 i = 0; i < size; i++) {
            dest[i] = rom[i];
        }
    }

    // Restore PCI
    outl(0xCF8, cmd_addr);
    outl(0xCFC, orig_cmd);
    outl(0xCF8, pci_addr);
    outl(0xCFC, orig_bar);

    // Write BDF to 0x0502 for boot.asm
    *(volatile u16*)0x0502 = (net_bus << 8) | (net_dev << 3) | net_func;

    return 1;
}
