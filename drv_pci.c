#include "drv_pci.h"

// Based on SeaBIOS hw/pci.c and hw/pcidevice.c
#define PORT_PCI_CMD  0x0cf8
#define PORT_PCI_DATA 0x0cfc

static u32 ioconfig_cmd(u16 bdf, u32 addr) {
    return 0x80000000 | (bdf << 8) | (addr & 0xfc);
}

u32 pci_readl(u16 bdf, u32 addr) {
    outl(PORT_PCI_CMD, ioconfig_cmd(bdf, addr));
    return inl(PORT_PCI_DATA);
}

u16 pci_readw(u16 bdf, u32 addr) {
    outl(PORT_PCI_CMD, ioconfig_cmd(bdf, addr));
    return inw(PORT_PCI_DATA + (addr & 2));
}

u8 pci_readb(u16 bdf, u32 addr) {
    outl(PORT_PCI_CMD, ioconfig_cmd(bdf, addr));
    return inb(PORT_PCI_DATA + (addr & 3));
}

void pci_writel(u16 bdf, u32 addr, u32 val) {
    outl(PORT_PCI_CMD, ioconfig_cmd(bdf, addr));
    outl(PORT_PCI_DATA, val);
}

#define MAX_PCI_DEVS 32
static pci_device_t pci_devs[MAX_PCI_DEVS];
static int num_pci_devs = 0;

void pci_init() {
    num_pci_devs = 0;
    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            for (int func = 0; func < 8; func++) {
                u16 bdf = (bus << 8) | (dev << 3) | func;
                u32 id = pci_readl(bdf, 0);
                if (id == 0xffffffff || id == 0) {
                    if (func == 0) break; // Skip remaining functions if func 0 is empty
                    continue;
                }
                
                if (num_pci_devs >= MAX_PCI_DEVS) return;
                
                pci_device_t *p = &pci_devs[num_pci_devs++];
                p->bdf = bdf;
                p->vendor = id & 0xffff;
                p->device = id >> 16;
                
                u32 classrev = pci_readl(bdf, 8);
                p->class_code = classrev >> 16;
                p->prog_if = (classrev >> 8) & 0xff;
                p->header_type = pci_readb(bdf, PCI_HEADER_TYPE);
                
                if ((p->header_type & 0x7f) == 1) { // Bridge
                    p->secondary_bus = pci_readb(bdf, 0x19);
                }
                
                if (func == 0 && (p->header_type & 0x80) == 0) {
                    break; // Not a multi-function device
                }
            }
        }
    }
}

pci_device_t* pci_find_class(u16 class_code) {
    for (int i = 0; i < num_pci_devs; i++) {
        if (pci_devs[i].class_code == class_code) {
            return &pci_devs[i];
        }
    }
    return 0; // NULL
}

pci_device_t* pci_find_device(u16 vendor, u16 device) {
    for (int i = 0; i < num_pci_devs; i++) {
        if (pci_devs[i].vendor == vendor && pci_devs[i].device == device) {
            return &pci_devs[i];
        }
    }
    return 0; // NULL
}

pci_device_t* pci_find_device_by_index(int index) {
    if (index >= 0 && index < num_pci_devs) {
        return &pci_devs[index];
    }
    return 0;
}
