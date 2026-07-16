[bits 16]

; Реализация базовых прерываний BIOS для загрузки GRUB/Windows/Linux
; Вдохновлено архитектурой SeaBIOS, но написано с нуля в минималистичном стиле.

global int08_handler
global int13_handler
global int15_handler
global int16_handler

; --- INT 08h (Таймер) ---
; Загрузчики часто проверяют ячейку памяти 0x046C (BIOS Timer Tick),
; чтобы отмерять таймауты (например, ожидание 3 секунды в меню).
; Если таймер не тикает, загрузчик может зависнуть навсегда!
int08_handler:
    push ds
    push ax
    xor ax, ax
    mov ds, ax
    inc dword [0x046C] ; Увеличиваем счетчик (18.2 раза в секунду)
    ; Send EOI (End of Interrupt) to Master PIC
    mov al, 0x20
    out 0x20, al
    pop ax
    pop ds
    iret

global serial_print_hex
serial_print_hex:
    pusha
    mov dx, 0x3F8 ; COM1
    mov cl, 4
    rol al, cl
    mov bl, al
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .p1
    add al, 7
.p1:
    out dx, al
    mov al, bl
    rol al, cl
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .p2
    add al, 7
.p2:
    out dx, al
    popa
    ret

global serial_print_char
serial_print_char:
    push dx
    mov dx, 0x3F8
    out dx, al
    pop dx
    ret

print_hex_byte:
    pusha
    mov bh, al
    shr al, 4
    call print_hex_nibble
    mov al, bh
    and al, 0x0F
    call print_hex_nibble
    popa
    ret

print_hex_nibble:
    cmp al, 10
    jl .num
    add al, 'A' - 10
    jmp .print
.num:
    add al, '0'
.print:
    call serial_print_char
    ret

; --- INT 09h (Hardware Keyboard IRQ1) ---
global int09_handler
int09_handler:
    pusha
    push ds
    push es
    xor ax, ax
    mov ds, ax
    mov es, ax
    
    in al, 0x60
    
    ; Check if key release
    test al, 0x80
    jnz .done
    ; Convert Scancode (AL) to ASCII (AH)
    mov ah, 0
    cmp al, 0x39
    ja .push_buf
    push bx
    mov bx, ax
    and bx, 0x00FF
    mov ah, [cs:(.scancode_to_ascii - payload_start) + bx]
    pop bx
    jmp .push_buf

.scancode_to_ascii:
    db 0, 27, '1','2','3','4','5','6','7','8','9','0','-','=',8, 9
    db 'q','w','e','r','t','y','u','i','o','p','[',']',13, 0,'a','s'
    db 'd','f','g','h','j','k','l',';',39,'`', 0,'\','z','x','c','v'
    db 'b','n','m',',','.','/', 0,'*', 0, ' ', 0, 0, 0, 0, 0, 0

.push_buf:
    ; AX = [ASCII, Scancode]. BIOS expects AH=Scancode, AL=ASCII.
    xchg ah, al ; Now AH=Scancode, AL=ASCII

    ; Push AX to circular buffer at Tail
    mov bx, [0x041C] ; Tail pointer
    mov di, bx
    add di, 2
    cmp di, 0x003E
    jne .no_wrap
    mov di, 0x001E
.no_wrap:
    cmp di, [0x041A] ; Compare next Tail with Head
    je .done ; Buffer full, drop key
    
    mov [bx + 0x0400], ax ; Write to buffer
    mov [0x041C], di ; Update tail

.done:
    ; Send EOI
    mov al, 0x20
    out 0x20, al
    pop es
    pop ds
    popa
    iret

; --- INT 16h (Клавиатура) ---
; Настоящая реализация работы с кольцевым буфером BDA
int16_handler:
    cmp ah, 0x01
    je .check_key
    cmp ah, 0x11
    je .check_key
    cmp ah, 0x02
    je .shift_status
    cmp ah, 0x12
    je .shift_status
    
    cmp ah, 0x00
    je .wait_key
    cmp ah, 0x10
    je .wait_key
    
    ; Unknown AH, just return
    iret

.shift_status:
    ; Return shift status in AL (0 = no shift/ctrl/alt)
    mov al, 0
    iret

    ; For AH=00h or AH=10h
.wait_key:
    push ds
    xor bx, bx
    mov ds, bx
.loop_wait:
    sti ; Разрешаем аппаратные прерывания, иначе hlt никогда не проснется!
    hlt ; Wait for any interrupt (saves CPU)
    cli ; Запрещаем на время чтения буфера
    
    mov bx, [0x041A] ; Head
    cmp bx, [0x041C] ; Tail
    je .loop_wait

    ; Get key
    mov ax, [bx + 0x0400]
    add bx, 2
    cmp bx, 0x003E
    jne .nw2
    mov bx, 0x001E
.nw2:
    mov [0x041A], bx ; Update head
    pop ds
    iret ; Return AX directly to caller!

.check_key:
    push ds
    xor bx, bx
    mov ds, bx
    
    mov bx, [0x041A] ; Head
    cmp bx, [0x041C] ; Tail
    je .empty
    
    ; Not empty: read key but DO NOT advance head
    mov ax, [bx + 0x0400]
    pop ds
    
    ; Clear ZF on stack to indicate key available
    push bp
    mov bp, sp
    and word [bp+6], 0xFFBF ; Clear ZF (bit 6)
    pop bp
    iret

.empty:
    pop ds
    
    ; Set ZF on stack to indicate buffer empty
    push bp
    mov bp, sp
    or word [bp+6], 0x0040 ; Set ZF (bit 6)
    pop bp
    iret

; --- INT 13h (Диск) ---
; GRUB использует LBA-расширения для чтения диска.
int13_handler:
    push ax
    mov al, 'D'
    call serial_print_char
    pop ax
    
    push ax
    mov al, ah
    call serial_print_hex
    mov al, 10
    call serial_print_char
    pop ax

    cmp ah, 0x41
    je .check_ext
    cmp ah, 0x42
    je .ext_read
    cmp ah, 0x48
    je .ext_get_params
    cmp ah, 0x02
    je .chs_read
    cmp ah, 0x08
    je .get_params
    
    ; Неподдерживаемая функция
    mov ah, 0x01
    stc
    jmp bios_return_from_int

.check_ext:
    mov bx, 0xAA55
    mov cx, 1
    mov ah, 0x30
    clc
    jmp bios_return_from_int

.ext_get_params:
    ; DS:SI points to the Result Buffer
    ; Buffer length is at [si]
    cmp word [si], 26
    jb .error_params ; Буфер слишком мал

    mov word [si], 26 ; Размер возвращаемых данных
    mov word [si+2], 0x0004 ; Флаги (бит 2: LBA/Extended read supported)
    mov dword [si+16], 0x00400000 ; Total sectors (Low 32-bit): 4,194,304 = 2GB
    mov dword [si+20], 0 ; Total sectors (High 32-bit)
    mov word [si+24], 512 ; Bytes per sector

    mov ah, 0x00
    clc
    jmp bios_return_from_int

.error_params:
    mov ah, 0x01
    stc
    jmp bios_return_from_int

.get_disk_type:
    cmp dl, 0x80
    jae .is_hdd
    mov ah, 0x00 ; No such drive
    clc
    jmp bios_return_from_int
.is_hdd:
    mov ah, 0x03 ; Hard disk
    mov cx, 0xFFFF
    mov dx, 0xFFFF
    clc
    jmp bios_return_from_int

.get_params:
    mov cx, 0xFFFF
    mov dh, 15
    mov dl, 1 ; 1 drive
    mov ah, 0
    clc
    jmp bios_return_from_int

.ext_read:
    ; Сохраняем все 32-битные регистры! GRUB полагается на них.
    pushad
    
    ; Setup variables
    mov ebx, 0
    mov bx, [si+2] ; Total Remaining Sectors
    
    mov eax, [si+8] ; LBA Low
    
    mov edx, 0
    mov dx, [si+6]
    mov es, dx ; Buffer segment
    
    mov edi, 0
    mov di, [si+4] ; Buffer offset

.read_chunk_loop:
    cmp ebx, 0
    je .read_done

    ; Print trace
    push ax
    mov al, 'C'
    call serial_print_char
    pop ax

    ; ECX = Chunk Size
    mov ecx, ebx
    cmp ecx, 127
    jbe .size_ok
    mov ecx, 127
.size_ok:

    ; Отключаем прерывания ATA (nIEN=1)
    mov dx, 0x3F6
    push ax
    mov al, 0x02
    out dx, al
    pop ax

    ; Wait for BSY=0 before selecting drive
    push edi
    mov edi, 0x000FFFFF
.wait_bsy1:
    dec edi
    jz .read_timeout_err
    mov dx, 0x1F7
    in al, dx
    test al, 0x80 ; BSY
    jnz .wait_bsy1
    pop edi

    ; Select Drive and send high bits of LBA (0x1F6)
    mov dx, 0x1F6
    push eax
    push ecx
    mov cl, 24
    shr eax, cl
    and al, 0x0F
    
    ; DL is the original drive number, passed in DL to int13_handler.
    ; But we overwrote DL at the start of .ext_read!
    ; We need to save the original DL! Let's get it from [si+2] NO, that's sector count.
    ; GRUB passes drive in DL! Let's read it from the stack!
    ; pushad pushes: EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI
    ; So EDX is at SP + 20
    push bp
    mov bp, sp
    mov cl, [bp+20] ; Original DL
    pop bp
    
    and cl, 1
    shl cl, 4
    or al, 0xE0
    or al, cl
    out dx, al
    pop ecx
    pop eax

    ; Wait for BSY=0 and RDY=1 after selecting drive
    push edi
    mov edi, 0x000FFFFF
.wait_rdy1:
    dec edi
    jz .read_timeout_err
    mov dx, 0x1F7
    in al, dx
    test al, 0x80 ; BSY
    jnz .wait_rdy1
    test al, 0x40 ; RDY
    jz .wait_rdy1
    pop edi

    ; Send Sector Count (CX)
    mov dx, 0x1F2
    push ax
    mov ax, cx
    out dx, al
    pop ax

    ; Send LBA Low (0x1F3)
    mov dx, 0x1F3
    out dx, al
    
    ; Send LBA Mid (0x1F4)
    mov dx, 0x1F4
    push eax
    push ecx
    mov cl, 8
    shr eax, cl
    out dx, al
    pop ecx
    pop eax

    ; Send LBA High (0x1F5)
    mov dx, 0x1F5
    push eax
    push ecx
    mov cl, 16
    shr eax, cl
    out dx, al
    pop ecx
    pop eax

    ; Send Read Command (0x20)
    mov dx, 0x1F7
    push ax
    mov al, 0x20
    out dx, al
    pop ax

    ; Save Chunk Size for loop
    push ecx

.read_sector_loop:
    ; Wait for DRQ
    mov dx, 0x3F6
    in al, dx
    in al, dx
    in al, dx
    in al, dx

    push edi
    mov edi, 0x000FFFFF
.wait_drq:
    dec edi
    jz .read_timeout_err
    mov dx, 0x1F7
    in al, dx
    test al, 0x80 ; BSY
    jnz .wait_drq
    test al, 0x01 ; ERR
    jnz .read_error_err
    test al, 0x08 ; DRQ
    jz .wait_drq
    pop edi

    ; Read 1 sector (256 words)
    cld
    mov dx, 0x1F0
    push ecx
    push edi ; Сохраняем DI, чтобы не было смещения
    mov cx, 256
    rep insw
    pop edi ; Восстанавливаем DI
    pop ecx

    ; Advance Buffer (ES only, DI stays original)
    push ax
    mov ax, es
    add ax, 0x0020
    mov es, ax
    pop ax
    
    dec cx
    jnz .read_sector_loop

    ; Restore Chunk Size
    pop ecx

    ; Advance LBA and Remaining Sectors
    add eax, ecx
    sub ebx, ecx

    jmp .read_chunk_loop

.read_timeout_err:
    pop edi
    jmp .read_error
.read_error_err:
    pop edi
.read_error:
    popad
    mov ah, 0x80
    stc
    jmp bios_return_from_int

.read_done:
    popad
    xor ax, ax ; Set AH=0 and AL=0 (Success)
    clc
    jmp bios_return_from_int

.chs_read:
    ; Convert CHS to LBA and read
    ; AL = count
    ; CH = cyl low, CL = cyl high (bits 6-7) | sector (bits 0-5)
    ; DH = head, DL = drive
    pusha
    push ds
    push es
    
    ; Calculate Cyl
    movzx eax, ch
    movzx ebx, cl
    and ebx, 0xC0
    shl ebx, 2
    or eax, ebx ; eax = Cyl
    
    ; Cyl * 16 + Head
    shl eax, 4
    movzx ebx, dh
    add eax, ebx
    
    ; * 63
    mov ebx, 63
    mul ebx
    
    ; + (Sector - 1)
    movzx ebx, cl
    and ebx, 0x3F
    dec ebx
    add eax, ebx
    
    ; EAX = LBA.
    ; Retrieve AL (count) from stack. `pusha` pushes 16 bytes. `push ds`, `push es` pushes 4 bytes.
    ; SP layout:
    ; [bp+18] = AX
    ; [bp+16] = CX
    ; [bp+14] = DX
    ; [bp+12] = BX
    ; [bp+10] = SP_orig
    ; [bp+8]  = BP_orig
    ; [bp+6]  = SI
    ; [bp+4]  = DI
    ; [bp+2]  = DS
    ; [bp+0]  = ES
    mov bp, sp
    mov cx, [bp+18] ; AX
    and cx, 0x00FF ; CX = count
    
    ; Setup BX
    mov bx, [bp+12] ; BX was pushed
    
.chs_read_loop:
    cmp cx, 0
    je .read_done

    push cx
    push eax ; СОХРАНЯЕМ EAX (LBA), так как wait_rdy затрет AL!
    ; Wait for ready
    mov edi, 0x00FFFFFF
.chs_wait_rdy:
    dec edi
    jz .chs_timeout

    mov dx, 0x1F7
    in al, dx
    test al, 0x80 ; Check BSY
    jnz .chs_wait_rdy
    test al, 0x40 ; Check RDY
    jz .chs_wait_rdy
    pop eax  ; ВОССТАНАВЛИВАЕМ EAX (LBA)

    ; Send sectors = 1
    mov dx, 0x1F2
    push ax
    mov al, 1
    out dx, al
    pop ax

    ; Send LBA Low (0x1F3)
    mov dx, 0x1F3
    out dx, al
    
    ; Send LBA Mid (0x1F4)
    mov dx, 0x1F4
    push eax
    push cx
    mov cl, 8
    shr eax, cl
    out dx, al
    pop cx
    pop eax
    
    ; Send LBA High (0x1F5)
    mov dx, 0x1F5
    push eax
    push cx
    mov cl, 16
    shr eax, cl
    out dx, al
    pop cx
    pop eax

    ; Select Drive and send high bits of LBA (0x1F6)
    mov dx, 0x1F6
    push eax
    push cx
    mov cl, 24
    shr eax, cl
    and al, 0x0F
    mov cl, dl
    and cl, 1
    shl cl, 4
    or al, 0xE0
    or al, cl
    out dx, al
    pop cx
    pop eax

    ; Send Read Command
    mov dx, 0x1F7
    push ax
    mov al, 0x20
    out dx, al
    pop ax

    push eax ; ЗАЩИЩАЕМ EAX ОТ ПЕРЕЗАПИСИ В WAIT_DRQ!
    mov edi, eax ; ЗАЩИЩАЕМ EAX (LBA)
    ; 400ns delay
    mov dx, 0x3F6
    in al, dx
    in al, dx
    in al, dx
    in al, dx

    mov edi, 0x00FFFFFF
.chs_wait_drq:
    dec edi
    jz .chs_timeout

    mov dx, 0x1F7
    in al, dx
    test al, 0x80 ; Check BSY
    jnz .chs_wait_drq ; Ignore ERR and DRQ if BSY is set!
    test al, 0x01 ; ERR bit
    jnz .chs_error
    test al, 0x08
    jz .chs_wait_drq
    mov eax, edi ; ВОССТАНАВЛИВАЕМ EAX
    pop eax

    ; Read 256 words (512 bytes)
    cld
    mov dx, 0x1F0
    push cx
    mov cx, 256
    mov di, bx
    rep insw
    pop cx

    ; Advance LBA and Buffer
    inc eax
    push ax
    mov ax, es
    add ax, 0x0020
    mov es, ax
    pop ax
    
    pop cx
    dec cx
    jmp .chs_read_loop

.chs_error:
    pop eax
    pop cx
    pop es
    pop ds
    popa
    mov ah, 0x01
    stc
    jmp bios_return_from_int

.chs_timeout:
    pop eax
    pop cx
    pop es
    pop ds
    popa
    mov ah, 0x80 ; Timeout error
    stc
    jmp bios_return_from_int

; --- INT 1Ah (Время/Таймер) ---
; SYSLINUX и другие загрузчики используют это для таймаутов!
global int1A_handler
int1A_handler:
    cmp ah, 0x00
    je .get_time
    
    stc
    jmp bios_return_from_int

.get_time:
    push bx
    push ds
    xor bx, bx
    mov ds, bx
    mov cx, [0x046E] ; High word
    mov dx, [0x046C] ; Low word
    pop ds
    pop bx
    mov al, 0
    clc
    jmp bios_return_from_int

bios_return_from_int:
    push bp
    mov bp, sp
    jc .set_carry
    and word [bp+6], 0xFFFE ; Clear CF
    jmp .ret_end
.set_carry:
    or word [bp+6], 0x0001  ; Set CF
.ret_end:
    pop bp
    iret


; --- INT 15h (ОЗУ / System Services) ---
int15_handler:
    push ax
    mov al, 'M'
    call serial_print_char
    pop ax
    
    push ax
    mov al, ah
    call serial_print_hex
    mov al, 10
    call serial_print_char
    pop ax

    cmp ah, 0x24
    je .a20_gate

    cmp ah, 0x88
    je .get_ext_mem
    
    cmp eax, 0xE820
    jne .unsupported
    cmp edx, 0x534D4150 ; 'SMAP'
    jne .unsupported
    
    cmp ebx, 0
    jne .map2
    
    ; Entry 1: 0 - 0xA0000 (Available RAM - 640 KB)
    mov dword [es:di], 0
    mov dword [es:di+4], 0
    mov dword [es:di+8], 0xA0000
    mov dword [es:di+12], 0
    mov dword [es:di+16], 1 ; Usable
    mov ebx, 1
    mov eax, 0x534D4150
    mov ecx, 20
    clc
    jmp bios_return_from_int

.map2:
    cmp ebx, 1
    jne .map_end
    
    ; Read CMOS 0x34 and 0x35 to calculate RAM size dynamically
    pusha
    mov al, 0x35
    out 0x70, al
    in al, 0x71
    mov bh, al
    mov al, 0x34
    out 0x70, al
    in al, 0x71
    mov bl, al
    movzx eax, bx
    shl eax, 16
    add eax, 16777216
    sub eax, 0x00100000
    mov [cs:.ram_size_tmp], eax
    popa
    
    mov dword [es:di], 0x00100000 ; BaseLow
    mov dword [es:di+4], 0      ; BaseHigh
    mov eax, [cs:.ram_size_tmp]
    mov dword [es:di+8], eax    ; LengthLow
    mov dword [es:di+12], 0     ; LengthHigh
    mov dword [es:di+16], 1     ; Type (1 = Usable)
    mov ebx, 0                  ; Next entry (0 = End of list!)
    mov eax, 0x534D4150
    mov ecx, 20
    clc
    jmp bios_return_from_int

.ram_size_tmp:
    dd 0

.map_end:
    mov ebx, 0
    stc
    jmp bios_return_from_int

.get_ext_mem:
    mov al, 0x31
    out 0x70, al
    in al, 0x71
    mov bh, al
    mov al, 0x30
    out 0x70, al
    in al, 0x71
    mov bl, al
    mov ax, bx
    clc
    jmp bios_return_from_int

.a20_gate:
    cmp al, 0x00 ; Disable
    je .a20_disable
    cmp al, 0x01 ; Enable
    je .a20_enable
    cmp al, 0x02 ; Status
    je .a20_status
    cmp al, 0x03 ; Query
    je .a20_query
    stc
    jmp bios_return_from_int

.a20_disable:
    in al, 0x92
    and al, 0xFD
    out 0x92, al
    clc
    mov ah, 0
    jmp bios_return_from_int

.a20_enable:
    in al, 0x92
    or al, 0x02
    out 0x92, al
    clc
    mov ah, 0
    jmp bios_return_from_int

.a20_status:
    in al, 0x92
    and al, 0x02
    shr al, 1
    clc
    mov ah, 0
    jmp bios_return_from_int

.a20_query:
    mov bx, 0x0002 ; Support port 0x92
    clc
    mov ah, 0
    jmp bios_return_from_int

.unsupported:
    stc
    jmp bios_return_from_int

; --- INT 11h (Equipment) ---
global int11_handler
int11_handler:
    push ds
    xor ax, ax
    mov ds, ax
    mov ax, [0x0410]
    pop ds
    iret

; --- INT 12h (Memory Size) ---
global int12_handler
int12_handler:
    push ds
    xor ax, ax
    mov ds, ax
    mov ax, [0x0413]
    pop ds
    iret
