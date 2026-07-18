extern bios_main
extern _data_start_rom
extern _data_start_ram
extern _data_end_ram
extern _bss_start
extern _bss_end

; === 32-БИТНОЕ ЯДРО (0xFFFE0000) ===
section .text
[bits 32]
start32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    ; --- C RUNTIME INIT ---
    ; Copy .data from ROM to RAM
    mov esi, _data_start_rom
    mov edi, _data_start_ram
    mov ecx, _data_end_ram
    sub ecx, _data_start_ram
    rep movsb
    
    ; Zero .bss
    mov edi, _bss_start
    mov ecx, _bss_end
    sub ecx, _bss_start
    xor al, al
    rep stosb

    call bios_main

hang:
    cli
    hlt
    jmp hang

global copy_and_jump_16
copy_and_jump_16:
    ; Отключение графического режима VBE
    mov dx, 0x01CE
    mov ax, 4
    out dx, ax
    mov dx, 0x01CF
    mov ax, 0
    out dx, ax

    mov esi, payload_start
    mov edi, 0x9E000
    mov ecx, payload_end - payload_start
    rep movsb
    jmp 0x08:0x9E000

; --- БЛОК 16-БИТНОГО КОДА (КОПИРУЕТСЯ В 0x9E000) ---
payload_start:
    db 0xEA
    dd 0x9E007 ; jump to next instruction to set CS
    dw 0x18
    db 0xB8, 0x20, 0x00
    db 0x8E, 0xD8
    db 0x8E, 0xC0
    db 0x8E, 0xD0
    db 0x0F, 0x20, 0xC0
    db 0x66, 0x25, 0xFE, 0xFF, 0xFF, 0xFF ; Turn off PG, PE
    db 0x0F, 0x22, 0xC0
    db 0xEA
    dw 0x0021 ; Jump to real mode (offset 0x0021)
    dw 0x9E00 ; Segment 0x9E00

    ; --- REAL MODE STARTS HERE (0x0000:0x1021) ---
    [bits 16]
    ; Set segments
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    ; --- CLEAR BDA BEFORE VGA ROM INIT ---
    ; To prevent VGA ROM from detecting a fake warm-boot and skipping font load
    mov di, 0x0400
    mov cx, 0x0080 ; 128 words = 256 bytes
    xor ax, ax
    rep stosw

    ; Инициализация VGA BIOS (Сброс графического режима в текстовый)
    mov ax, 0xC000
    mov ds, ax
    cmp word [0], 0xAA55
    jne .no_vga
    ; Call VGA ROM Init
    ; AX must contain PCI BDF (Bus 0, Dev 2, Func 0 = 0x0010)
    mov ax, 0x0010
    call 0xC000:0003
    
    ; Force Text Mode (80x25) using standard INT 10h
    mov ax, 0x0003
    int 0x10
.no_vga:
    xor ax, ax
    mov ds, ax

    ; --- POST (Power-On Self Test) ---
    ; Включение линии A20 (Fast A20) через порт 0x92
    in al, 0x92
    or al, 2
    out 0x92, al

    ; Отключаем AUX (Мышь) порт, чтобы случайные движения не забили буфер 8042
.wait_disable_aux:
    in al, 0x64
    test al, 2
    jnz .wait_disable_aux
    mov al, 0xA7 ; Disable Mouse Port
    out 0x64, al
    
    ; Очищаем буфер PS/2 (вычитываем весь мусор)
.flush_ps2:
    in al, 0x64
    test al, 1
    jz .ps2_flushed
    in al, 0x60
    jmp .flush_ps2
.ps2_flushed:

    ; Инициализация 8042 PS/2 контроллера (Включаем IRQ1 и трансляцию в Set 1)
.wait_8042_1:
    in al, 0x64
    test al, 2
    jnz .wait_8042_1
    mov al, 0x60 ; Команда записи Command Byte
    out 0x64, al
.wait_8042_2:
    in al, 0x64
    test al, 2
    jnz .wait_8042_2
    mov al, 0x47 ; IRQ1=1, SysFlag=1, Translate=1 (AUX IRQ=0)
    out 0x60, al

    ; Инициализация PIT (Таймер) 18.2 Hz
    mov al, 0x36 ; Channel 0, LSB/MSB, Mode 3, Binary
    out 0x43, al
    xor al, al
    out 0x40, al ; LSB = 0
    out 0x40, al ; MSB = 0 (Divisor 65536)

    ; Разрешаем прерывания таймера (IRQ0) и клавиатуры (IRQ1) в PIC
    in al, 0x21
    and al, 0xFC ; Clear bit 0 and 1
    out 0x21, al

    ; Инициализация буфера клавиатуры в BDA (0x041A = Head, 0x041C = Tail)
    mov word [0x041A], 0x001E
    mov word [0x041C], 0x001E
    ; Инициализация базовой памяти BDA (632 КБ - оставляем 8 КБ для EBDA BIOS)
    mov word [0x0413], 632
    ; Инициализация оборудования BDA (0x0410) - VGA + FPU
    mov word [0x0410], 0x0022

    ; Установка обработчиков прерываний (IVT)
    cli
    ; INT 08h (Timer)
    mov word [0x0000 + 0x08*4], (int08_handler - payload_start)
    mov word [0x0000 + 0x08*4 + 2], 0x9E00
    ; INT 09h (Keyboard IRQ1)
    mov word [0x0000 + 0x09*4], (int09_handler - payload_start)
    mov word [0x0000 + 0x09*4 + 2], 0x9E00
    ; INT 11h (Equipment)
    mov word [0x0000 + 0x11*4], (int11_handler - payload_start)
    mov word [0x0000 + 0x11*4 + 2], 0x9E00
    ; INT 12h (Memory Size)
    mov word [0x0000 + 0x12*4], (int12_handler - payload_start)
    mov word [0x0000 + 0x12*4 + 2], 0x9E00
    ; INT 13h (Disk)
    mov word [0x0000 + 0x13*4], (int13_handler - payload_start)
    mov word [0x0000 + 0x13*4 + 2], 0x9E00
    ; INT 15h
    mov word [0x0000 + 0x15*4], (int15_handler - payload_start)
    mov word [0x0000 + 0x15*4 + 2], 0x9E00
    ; INT 16h
    mov word [0x0000 + 0x16*4], (int16_handler - payload_start)
    mov word [0x0000 + 0x16*4 + 2], 0x9E00
    ; INT 1Ah
    mov word [0x0000 + 0x1A*4], (int1A_handler - payload_start)
    mov word [0x0000 + 0x1A*4 + 2], 0x9E00

    ; Инициализация PIT (Таймер) на 18.2 Гц (делитель 65536)
    mov al, 0x36
    out 0x43, al
    xor al, al
    out 0x40, al
    out 0x40, al

    ; Инициализация BDA (BIOS Data Area)
    mov word [0x0400], 0x03F8 ; COM1
    mov word [0x0402], 0x0000 ; COM2
    mov word [0x0404], 0x0000 ; COM3
    mov word [0x0406], 0x0000 ; COM4
    mov word [0x0410], 0x0221 ; 1 COM port, 1 FPU
    mov word [0x0413], 632    ; Base Memory (KB)
    mov byte [0x0475], 1      ; Number of HDDs

    ; Полная инициализация контроллеров прерываний PIC (Master & Slave)
    ; ICW1
    mov al, 0x11
    out 0x20, al
    out 0xA0, al
    ; ICW2 (Master IRQ0-7 -> INT 08h-0Fh, Slave IRQ8-15 -> INT 70h-77h)
    mov al, 0x08
    out 0x21, al
    mov al, 0x70
    out 0xA1, al
    ; ICW3
    mov al, 0x04
    out 0x21, al
    mov al, 0x02
    out 0xA1, al
    ; ICW4
    mov al, 0x01
    out 0x21, al
    out 0xA1, al

    ; Разблокировка IRQ0 (Таймер), IRQ1 (Клавиатура), IRQ2 (Cascade) на Master, остальные глушим
    mov al, 0xF8 ; 1111 1000
    out 0x21, al
    mov al, 0xFF
    out 0xA1, al

    sti

    mov dl, [0x0500] ; Read boot_device from memory
    cmp dl, 0x12
    je .pxe_boot

    ; Normal Boot (HDD/Floppy)
    jmp 0x0000:0x7C00

.pxe_boot:
    ; Init PXE ROM at 0xC8000
    push ds
    mov ax, 0xC800
    mov ds, ax
    cmp word [0], 0xAA55
    pop ds
    jne .no_pxe

    ; Setup registers for PCI Option ROM Init
    mov ax, [0x0502] ; AX = PCI BDF (Bus/Dev/Func)
    xor di, di
    mov es, di       ; ES:DI = 0000:0000 (No PnP)
    
    ; CALL FAR C800:0003
    call 0xC800:0003

    ; Now execute BEV (Bootstrap Entry Vector) if present
    mov ax, 0xC800
    mov ds, ax
    mov bx, [0x001A] ; PnP Header offset
    cmp bx, 0
    je .no_pxe
    
    cmp dword [bx], 0x506E5024 ; '$PnP'
    jne .no_pxe
    
    mov cx, [bx + 0x1A] ; BEV offset
    cmp cx, 0
    je .no_pxe
    
    ; We have a BEV! Far jump to it (BEV should not return, it boots)
    ; We push the segment and offset, then retf
    push ax
    push cx
    retf

.no_pxe:
    xor ax, ax
    mov ds, ax
    int 0x19 ; Execute Bootstrap Loader
    
    ; If INT 19h fails to boot, halt
    cli
.halt:
    hlt
    jmp .halt

    %include "drv_bios_ints.asm"

payload_end:


; === 16-БИТНЫЙ БУТБЛОК (0xFFFFFF00) ===
section .boot
[bits 16]
align 4
gdt_start:
    dd 0, 0
gdt_code:
    dw 0xFFFF, 0x0000
    db 0x00, 0x9A, 0xCF, 0x00
gdt_data:
    dw 0xFFFF, 0x0000
    db 0x00, 0x92, 0xCF, 0x00
gdt_code16:
    dw 0xFFFF, 0x0000
    db 0x00, 0x9A, 0x0F, 0x00
gdt_data16:
    dw 0xFFFF, 0x0000
    db 0x00, 0x92, 0x0F, 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

_start_real:
    cli
    cld

    mov bx, (gdt_descriptor - $$) + 0xFF00
    db 0x66
    lgdt [cs:bx]

    mov eax, cr0
    or al, 1
    mov cr0, eax

    jmp dword 0x08:start32

times 240 - ($ - $$) db 0

[bits 16]
global reset_vector
reset_vector:
    jmp _start_real

times 255 - ($ - $$) db 0
db 0x90
