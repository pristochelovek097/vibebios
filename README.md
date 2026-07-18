# VIBEBIOS 🖥️

[🇷🇺 Русский](README_ru.md) | [🇬🇧 English](README.md)

**VIBEBIOS** is a custom 32-bit graphical firmware (ROM BIOS) written from scratch in C and Assembly for the x86 architecture (specifically targeted for the QEMU emulator).

Unlike standard bootloaders (which load at `0x7C00`), this project is a **real BIOS**. It compiles into a 128 KB binary ROM file, maps to the very end of the motherboard's 4 GB address space, and intercepts execution right from the processor's Reset Vector (`0xFFFFFFF0`).

## 🌟 Features

* **Instant 32-bit Mode:** No waiting in 16-bit real mode. The BIOS sets up the GDT and jumps into Protected Mode in the first microseconds of startup.
* **Proper C Runtime:** Dynamically relocates `.data` to RAM and zeroes `.bss` to provide a fully functional C environment for static/global variables.
* **ACPI & SMBIOS Support:** Generates valid ACPI (RSDP, RSDT) and SMBIOS tables in memory, fully compatible with modern OS checks.
* **CMOS Persistence:** Non-volatile storage of BIOS settings (such as IDE/ACPI toggles and UI fonts) in the RTC CMOS SRAM.
* **PCI Device Tree:** Scans the PCI bus and renders a hierarchical hardware tree directly in the BIOS Setup.
* **High-Res Graphics (800x600):** Communicates with the PCI bus to configure the GPU and enable the Linear Frame Buffer (LFB) with 32-bit ARGB colors.
* **Custom GUI:** Integrated graphical setup utility (F2) with mouse support, popups, and dynamic fonts (Fixedsys, Sun).
* **Boot Devices:** Hardware-level drivers for ATA PIO (Hard Drives), Floppy Drives, and Network (PXE) booting.
* **Legacy OS Booting:** Validates the `0x55 0xAA` signature, sets up a dummy 16-bit IVT, disables conflicting PS/2 devices, and safely switches back to Real Mode to boot legacy OSs (e.g., GRUB, Ubuntu).

## 🛠️ Dependencies

To build the project on Linux, you will need:
* `gcc` (with `-m32` support)
* `nasm` (Assembler)
* `binutils` (for `ld` and `objcopy`)
* `qemu-system-x86_64` or `qemu-system-i386` (Emulator)
* `make`

## 🚀 Quick Start

1. Clone the repository.
2. Run the following command in the terminal to compile and launch:
   ```bash
   make run
   ```
3. The project will compile the BIOS ROM (`bios.bin`), build the test OS (`os.bin`), and launch QEMU.
4. To clean the directory of compiled files, use:
   ```bash
   make clean
   ```

## 📂 Project Structure

* `boot.asm` — The initial assembly code, Processor Reset Vector, GDT, C runtime init, and 32-bit transition.
* `bios.c` — The core BIOS logic in C (VGA, PCI, GUI, Menus, Boot Logic).
* `drv_*.c` — Hardware drivers (ATA, Floppy, Keyboard, PXE, PCI).
* `acpi.c` / `smbios.c` — System tables generation.
* `linker.ld` — The linker script defining the 128 KB ROM architecture and RAM relocation segments.
* `os.asm` — A minimal test OS to demonstrate execution handoff from the BIOS.
* `Makefile` — Build automation script.

## ⚠️ Disclaimer

VibeBIOS is an experimental project. It is heavily inspired by projects like SeaBIOS, Insyde, and Coreboot. While it implements a wide array of standard BIOS features (ACPI, CMOS, PCI, Real-Mode transitions), it may still lack full compatibility with obscure legacy systems.

*Note: This repository and its features were developed with the assistance of an AI agent.*
