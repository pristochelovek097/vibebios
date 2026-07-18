typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

struct smbios_entry {
    char anchor[4];
    u8 checksum;
    u8 length;
    u8 major_ver;
    u8 minor_ver;
    u16 max_struct_size;
    u8 eps_revision;
    char formatted_area[5];
    char intermediate_anchor[5];
    u8 intermediate_checksum;
    u16 struct_table_length;
    u32 struct_table_address;
    u16 num_structs;
    u8 bcd_revision;
} __attribute__((packed));

struct smbios_type0 {
    u8 type;
    u8 length;
    u16 handle;
    u8 vendor;
    u8 bios_version;
    u16 bios_start_addr;
    u8 bios_release_date;
    u8 bios_rom_size;
    u32 bios_characteristics[2];
    u8 bios_characteristics_ext[2];
    u8 sys_bios_major;
    u8 sys_bios_minor;
    u8 ec_major;
    u8 ec_minor;
} __attribute__((packed));

struct smbios_type1 {
    u8 type;
    u8 length;
    u16 handle;
    u8 manufacturer;
    u8 product_name;
    u8 version;
    u8 serial_number;
    u8 uuid[16];
    u8 wakeup_type;
    u8 sku_number;
    u8 family;
} __attribute__((packed));

struct smbios_type2 {
    u8 type;
    u8 length;
    u16 handle;
    u8 manufacturer;
    u8 product_name;
    u8 version;
    u8 serial_number;
    u8 asset_tag;
    u8 feature_flags;
    u8 location_in_chassis;
    u16 chassis_handle;
    u8 board_type;
    u8 num_contained_objects;
} __attribute__((packed));

struct smbios_type127 {
    u8 type;
    u8 length;
    u16 handle;
} __attribute__((packed));

struct smbios_payload {
    struct smbios_type0 t0;
    char t0_str[64];
    struct smbios_type1 t1;
    char t1_str[64];
    struct smbios_type2 t2;
    char t2_str[64];
    struct smbios_type127 t127;
    char t127_str[2];
} __attribute__((packed));

struct smbios_full {
    struct smbios_entry eps;
    struct smbios_payload payload;
} __attribute__((packed));

static u8 compute_checksum(void* data, int len) {
    u8 sum = 0;
    for(int i = 0; i < len; i++) sum += ((u8*)data)[i];
    return (u8)(-(char)sum);
}

#include "io.h"

void init_smbios() {
    // We will place SMBIOS at 0x000F0000
    struct smbios_full* smb = (struct smbios_full*)0x000F0000;
    
    // Unlock PAM0 for 0xF0000 - 0xFFFFF
    outl(0xCF8, 0x80000058);
    u32 pam = inl(0xCFC);
    outl(0xCF8, 0x80000058);
    outl(0xCFC, pam | 0x00300000); // Set PAM0 (0x59) to 0x30 (R/W)
    
    // Initialize to zero
    for (int i = 0; i < sizeof(struct smbios_full); i++) ((u8*)smb)[i] = 0;
    
    smb->eps.anchor[0] = '_'; smb->eps.anchor[1] = 'S'; smb->eps.anchor[2] = 'M'; smb->eps.anchor[3] = '_';
    smb->eps.length = 0x1F;
    smb->eps.major_ver = 2; smb->eps.minor_ver = 4;
    smb->eps.max_struct_size = sizeof(struct smbios_payload);
    smb->eps.eps_revision = 0;
    smb->eps.intermediate_anchor[0] = '_'; smb->eps.intermediate_anchor[1] = 'D'; smb->eps.intermediate_anchor[2] = 'M'; smb->eps.intermediate_anchor[3] = 'I'; smb->eps.intermediate_anchor[4] = '_';
    smb->eps.struct_table_length = sizeof(struct smbios_payload);
    smb->eps.struct_table_address = 0x000F0000 + sizeof(struct smbios_entry);
    smb->eps.num_structs = 4;
    smb->eps.bcd_revision = 0x24;

    // Type 0
    smb->payload.t0.type = 0; smb->payload.t0.length = sizeof(struct smbios_type0); smb->payload.t0.handle = 0x0000;
    smb->payload.t0.vendor = 1; smb->payload.t0.bios_version = 2; smb->payload.t0.bios_start_addr = 0xE000;
    smb->payload.t0.bios_release_date = 3; smb->payload.t0.bios_rom_size = 1; // 128K
    smb->payload.t0.bios_characteristics[0] = 0x00080000; // PCI is supported
    smb->payload.t0.sys_bios_major = 4; smb->payload.t0.sys_bios_minor = 0;
    
    char* t0_str = smb->payload.t0_str;
    char* src = "Huinya Software Corp.\0VibeBIOS v4.0 Pro\00007/18/2026\0";
    for(int i=0; i<60; i++) t0_str[i] = src[i];
    
    // Type 1
    smb->payload.t1.type = 1; smb->payload.t1.length = sizeof(struct smbios_type1); smb->payload.t1.handle = 0x0100;
    smb->payload.t1.manufacturer = 1; smb->payload.t1.product_name = 2; smb->payload.t1.version = 3; smb->payload.t1.serial_number = 4;
    smb->payload.t1.wakeup_type = 6;
    
    // Read UUID from QEMU fw_cfg
    outw(0x510, 0x0203);
    for (int i = 0; i < 16; i++) smb->payload.t1.uuid[i] = inb(0x511);

    char* t1_str = smb->payload.t1_str;
    src = "Huinya Systems\0VibeMachine Pro 3000\0Rev 1\0""123456789\0";
    for(int i=0; i<60; i++) t1_str[i] = src[i];

    // Type 2
    smb->payload.t2.type = 2; smb->payload.t2.length = sizeof(struct smbios_type2); smb->payload.t2.handle = 0x0200;
    smb->payload.t2.manufacturer = 1; smb->payload.t2.product_name = 2; smb->payload.t2.version = 3; smb->payload.t2.serial_number = 4;
    smb->payload.t2.board_type = 0xA;
    
    char* t2_str = smb->payload.t2_str;
    src = "Huinya Boards\0VibeBoard Ultra\0v1.0\0SN-9999\0";
    for(int i=0; i<60; i++) t2_str[i] = src[i];
    
    // Type 127
    smb->payload.t127.type = 127; smb->payload.t127.length = sizeof(struct smbios_type127); smb->payload.t127.handle = 0x7F00;
    smb->payload.t127_str[0] = 0; smb->payload.t127_str[1] = 0;

    // Checksums
    smb->eps.intermediate_checksum = 0;
    smb->eps.intermediate_checksum = compute_checksum(&smb->eps.intermediate_anchor, 0x0F);
    
    smb->eps.checksum = 0;
    smb->eps.checksum = compute_checksum(&smb->eps, 0x1F);
    
    // Make PAM0 read-only again! (0xF0000 - 0xFFFFF = ROM)
    // Wait, if we make it read-only, it will map to ROM! 
    // And our SMBIOS in RAM will be HIDDEN!
    // So we MUST leave PAM0 as R/W (or at least READ-RAM).
    // In QEMU PAM, 0x30 = Read/Write RAM. 0x00 = Read ROM, Write RAM (doesn't exist? Actually 0x00 is disabled, so ROM).
    // 0x10 = Read RAM, Write ROM (ignored). 0x20 = Read ROM, Write RAM. 0x30 = Read RAM, Write RAM.
    // So we leave it at 0x30!
}
