CC      = gcc
AS      = nasm
LD      = ld
OBJCOPY = objcopy
QEMU    = qemu-system-i386

CFLAGS  = -m32 -ffreestanding -fno-pie -fno-stack-protector -Os -Wall -Wextra
ASFLAGS = -f elf32
LDFLAGS = -m elf_i386 -T linker.ld

TARGET  = bios.bin
ELF     = bios.elf
OBJS    = boot.o bios.o drv_ata.o drv_kbd.o drv_floppy.o drv_pxe.o
OS_BIN  = os.bin

all: $(TARGET) $(OS_BIN)

run: $(TARGET) $(OS_BIN)
	@echo ">> Запуск WILIXBIOS в QEMU..."
	$(QEMU) -m 32M -vga std -bios $(TARGET) -d int,cpu_reset -no-reboot -no-shutdown -drive file=$(OS_BIN),format=raw,index=0,media=disk -D qemu.log

$(TARGET): $(ELF)
	$(OBJCOPY) -O binary $< $@

$(ELF): $(OBJS)
	$(LD) $(LDFLAGS) $^ -o $@

boot.o: boot.asm
	$(AS) $(ASFLAGS) $< -o $@

bios.o: bios.c
	$(CC) $(CFLAGS) -c $< -o $@

drv_ata.o: drv_ata.c
	$(CC) $(CFLAGS) -c $< -o $@

drv_kbd.o: drv_kbd.c
	$(CC) $(CFLAGS) -c $< -o $@

drv_floppy.o: drv_floppy.c
	$(CC) $(CFLAGS) -c $< -o $@

drv_pxe.o: drv_pxe.c
	$(CC) $(CFLAGS) -c $< -o $@

# Компиляция нашей тестовой ОС
$(OS_BIN): os.asm
	@echo ">> Сборка тестовой ОС..."
	$(AS) -f bin $< -o $@

clean:
	rm -f *.o $(ELF) $(TARGET) $(OS_BIN)

.PHONY: all run clean