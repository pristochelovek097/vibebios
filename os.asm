org 0x7C00          ; Жестко говорим компилятору, что код будет лежать по адресу 0x7C00
bits 16             ; Работаем в 16-битном Real Mode

start:
    ; 1. Инициализируем сегментные регистры в 0
    cli             ; Запрещаем аппаратные прерывания на время настройки
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00  ; Устанавливаем безопасный стек, растущий вниз от 0x7C00
    sti             ; Разрешаем прерывания обратно

    ; 2. Очищаем экран и настраиваем видеорежим (80x25 текст, 16 цветов)
    mov ah, 0x00
    mov al, 0x03    ; Стандартный текстовый режим VGA
    int 0x10

    ; 3. Выводим приветственное сообщение
    mov si, welcome_msg
    call print_string

main_loop:
    ; 4. Выводим prompt (приглашение к вводу)
    mov si, prompt_msg
    call print_string

.wait_key:
    ; 5. Опрашиваем клавиатуру через INT 16h
    mov ah, 0x00    ; Функция 00h: ждать нажатия клавиши
    int 0x16        ; Вызов BIOS. На выходе: AH = скан-код, AL = ASCII символ

    ; Если нажали Enter (ASCII 13), переводим строку
    cmp al, 13
    je .handle_enter

    ; Иначе просто выводим нажатый символ на экран (эхо-ввод)
    mov ah, 0x0E    ; Функция 0Eh: телетайпный вывод символа
    int 0x10
    jmp .wait_key

.handle_enter:
    mov si, newline
    call print_string
    jmp main_loop   ; Возвращаемся к началу цикла

; --- ФУНКЦИЯ ВЫВОДА СТРОКИ ---
; Вход: SI указывает на строку, заканчивающуюся нулем (null-terminated)
print_string:
    push ax
    push bx
.loop:
    lodsb           ; Загружает байт из [DS:SI] в AL и инкрементирует SI
    or al, al       ; Проверяем, не дошли ли до конца строки (AL == 0)
    jz .done
    mov ah, 0x0E    ; BIOS функция вывода символа
    mov bh, 0x00    ; Номер видео-страницы
    mov bl, 0x07    ; Цвет текста (серый на черном)
    int 0x10
    jmp .loop
.done:
    pop bx
    pop ax
    ret

; --- ДАННЫЕ СИСТЕМЫ ---
welcome_msg  db '--- VIBE-OS v0.1 LOADED SUCCESSFULLY ---', 13, 10, 'Welcome to bare-metal world!', 13, 10, 0
prompt_msg   db 13, 10, 'vibe-os> ', 0
newline      db 13, 10, 0

; --- МАГИЧЕСКАЯ ЗАГЛУШКА ЗАГРУЗЧИКА ---
times 510-($-$$) db 0   ; Забиваем нулями всё пространство до 510-го байта
dw 0xAA55               ; Последние 2 байта: обязательная сигнатура MBR для BIOS
