#include "drv_ata.h"

// --- Функции из SeaBIOS (seabios/src/hw/ata.c) ---
static void ndelay_400ns() {
    inb(0x3F6); inb(0x3F6); inb(0x3F6); inb(0x3F6);
}

static int await_not_bsy() {
    while (1) {
        u8 status = inb(0x1F7);
        if (!(status & 0x80)) return status; // Ждем пока BSY=0
    }
}

static int await_rdy() {
    while (1) {
        u8 status = await_not_bsy();
        if (status & 0x40) return status; // Ждем RDY=1
    }
}

void ata_read_sectors(u8* buffer, u32 lba, u8 count) {
    await_rdy(); // SeaBIOS: дождаться готовности диска перед отправкой LBA

    // Отправляем LBA (тоже по стандарту SeaBIOS)
    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));
    outb(0x1F2, count);
    outb(0x1F3, (u8)lba);
    outb(0x1F4, (u8)(lba >> 8));
    outb(0x1F5, (u8)(lba >> 16));
    
    // Command: Read
    outb(0x1F7, 0x20); 

    ndelay_400ns(); // SeaBIOS: 400ns delay после отправки команды

    u16* ptr = (u16*)buffer;
    for(int s = 0; s < count; s++) {
        // SeaBIOS: ждем DRQ только когда BSY=0
        while (1) {
            u8 status = await_not_bsy();
            if (status & 0x01) return; // Ошибка чтения
            if (status & 0x08) break;  // DRQ=1, данные готовы
        }
        
        // Чтение данных
        for(int i = 0; i < 256; i++) {
            *ptr++ = inw(0x1F0);
        }
    }
}
