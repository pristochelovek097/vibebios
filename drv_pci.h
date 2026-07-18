#ifndef DRV_PCI_H
#define DRV_PCI_H

#include "io.h"

// PCI Configuration Space Registers
#define PCI_VENDOR_ID           0x00
#define PCI_DEVICE_ID           0x02
#define PCI_COMMAND             0x04
#define PCI_STATUS              0x06
#define PCI_REVISION_ID         0x08
#define PCI_PROG_IF             0x09
#define PCI_SUBCLASS            0x0a
#define PCI_CLASS_CODE          0x0b
#define PCI_HEADER_TYPE         0x0e
#define PCI_BAR0                0x10
#define PCI_BAR1                0x14
#define PCI_BAR2                0x18
#define PCI_BAR3                0x1c
#define PCI_BAR4                0x20
#define PCI_BAR5                0x24

typedef struct {
    u16 bdf; // Bus, Device, Function
    u16 vendor;
    u16 device;
    u16 class_code;
    u8 prog_if;
    u8 header_type;
    u8 secondary_bus;
} pci_device_t;

void pci_init();
u32 pci_readl(u16 bdf, u32 addr);
u16 pci_readw(u16 bdf, u32 addr);
u8 pci_readb(u16 bdf, u32 addr);
void pci_writel(u16 bdf, u32 addr, u32 val);
pci_device_t* pci_find_class(u16 class_code);
pci_device_t* pci_find_device(u16 vendor, u16 device);
pci_device_t* pci_find_device_by_index(int index);

#endif
