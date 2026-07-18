# VIBEBIOS: Technical Documentation 🧠

[🇷🇺 Русский](docs_ru.md) | [🇬🇧 English](docs.md)

This document describes the internal architecture of VibeBIOS and the technical solutions applied to bypass x86 hardware limitations.

## 1. ROM Memory Architecture (linker.ld)
According to the x86 standard, the processor starts executing instructions at the physical address `0xFFFFFFF0` (Reset Vector). QEMU maps the firmware file (`bios.bin`) to the very end of the 4 GB RAM.

Our BIOS is exactly 128 KB. Its memory map looks like this:
* `0xFFFE0000` — Entry point to the 32-bit C code (`bios_main`).
* `0xFFFFFF00` — 16-bit bootblock (segment setup, GDT).
* `0xFFFFFFF0` — Reset Vector (contains a far jump instruction back to the bootblock).

**Important:** The `objcopy` utility has a tendency to trim trailing zeroes from the binary. To ensure the file is exactly 131072 bytes, a `NOP` (`0x90`) instruction is added at the very end of `boot.asm`.

### C Runtime (RAM Relocation)
To support global and static variables in C, the `.data` and `.bss` sections cannot stay in the Read-Only Memory (ROM). The linker script defines a VMA (Virtual Memory Address) for these sections at `0x00010000` (in RAM), but keeps their LMA (Load Memory Address) in the ROM. 
During boot (`boot.asm`), the BIOS manually copies the `.data` section from ROM to RAM and zeroes out the `.bss` section before jumping to the C code.

## 2. Initialization and 32-bit Transition (boot.asm)
One of the most complex stages is parsing the GDT from ROM while still in 16-bit mode.
A historical x86 processor bug is that the 16-bit `lgdt` instruction only reads 24 bits of the base address, discarding the highest byte. This causes a `Triple Fault` because the address `0xFFFE0000` turns into `0x00FE0000`.

**Solution:** We apply the operand-size override hardware prefix `0x66` before the `lgdt` command.
```nasm
db 0x66
lgdt [cs:bx] ; The processor correctly reads the 32-bit GDT address from ROM
```

## 3. PCI Initialization and High-Res Graphics (bios.c)
The standard VGA address `0xA0000` is limited to 64 KB. This is enough for 320x200 (64,000 bytes) but not enough for 800x600 (480,000 bytes).
To bypass this limit, the **LFB (Linear Frame Buffer)** of the video card is used via the PCI bus.

The process in `init_vga()`:
1. Access the PCI configuration port (`0xCF8` / `0xCFC`).
2. Forcibly write the physical address `0xE0000000` into the video card's `BAR0`.
3. Enable the *Memory Space Enable* bit in the PCI Command Register (otherwise, the northbridge will block pixel transfers).
4. Initialize QEMU Bochs VBE (ports `0x01CE` / `0x01CF`) for 800x600x32bit ARGB resolution.

*Note:* The pointer to video memory is declared as `volatile u32* LFB`. This prevents aggressive GCC optimization (`-Os`), forcing it to physically write data to RAM when rendering each pixel.

## 4. HDD Reading (ATA PIO)
The BIOS does not have access to `int 13h`. We wrote a custom hard drive driver that communicates directly with the motherboard's IDE controller.
Algorithm in ATA read:
1. Poll port `0x1F7` until the busy bit (`BSY`) is cleared.
2. Write parameters to the ports (Sector LBA, Master disk).
3. Send the read command (`0x20`).
4. Wait for the data request flag (`DRQ`).
5. Read 256 words (512 bytes) from the data port `0x1F0` into RAM.

## 5. Persistence (CMOS)
To persist settings across resets, VibeBIOS utilizes the RTC CMOS SRAM. 
Custom bytes `0x40` and `0x41` are used to pack binary flags (IDE, ACPI, USB toggles) and indices (selected font). Byte `0x3F` is used as a magic signature (`0xAA`) to detect if the CMOS has been properly initialized.

## 6. Legacy OS Interaction (os.asm)
When successfully finding a bootloader signature (`0x55 0xAA`), the BIOS passes control to `0x7C00`.
To support legacy OSs that expect Real Mode (like GRUB or Ubuntu's bootloader):
1. A dummy 16-bit IVT (Interrupt Vector Table) is created at `0x0000`.
2. The PS/2 controller buffer is flushed to prevent keyboard hangups.
3. The processor safely drops back to 16-bit Real Mode via a far jump, reloading all segment descriptors with 16-bit limits.
