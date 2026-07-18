OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(reset_vector)

SECTIONS
{
    /* База ROM (128 KB) */
    . = 0xFFFE0000;
    
    .text : { *(.text) }
    .rodata : { *(.rodata*) }
    
    _data_start_rom = .;

    /* RAM Section */
    . = 0x00010000;
    _data_start_ram = .;
    .data : AT(_data_start_rom) { *(.data) }
    _data_end_ram = .;
    
    .bss : { 
        _bss_start = .;
        *(.bss) *(COMMON) 
        _bss_end = .;
    }

    /* Бутблок - последние 256 байт */
    . = 0xFFFFFF00;
    .boot : AT(0xFFFFFF00) { *(.boot) }
    
    /DISCARD/ : {
        *(.eh_frame)
        *(.comment)
    }
}