typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

struct acpi_rsdp {
    char signature[8];
    u8 checksum;
    char oem_id[6];
    u8 revision;
    u32 rsdt_address;
} __attribute__((packed));

struct acpi_header {
    char signature[4];
    u32 length;
    u8 revision;
    u8 checksum;
    char oem_id[6];
    char oem_table_id[8];
    u32 oem_revision;
    u32 creator_id;
    u32 creator_revision;
} __attribute__((packed));

struct acpi_rsdt {
    struct acpi_header header;
    u32 pointers[4]; // Let's just have space for some tables, though we might not have any
} __attribute__((packed));

extern void outl(u16 port, u32 data);
extern u32 inl(u16 port);

static u8 compute_checksum(void* data, int len) {
    u8 sum = 0;
    for(int i = 0; i < len; i++) sum += ((u8*)data)[i];
    return (u8)(-(char)sum);
}

void init_acpi() {
    // We will place ACPI RSDP at 0x000E0000 (PAM1 needs to be unlocked or we just use PAM0 space)
    // Actually, OS searches 0xE0000 to 0xFFFFF for RSDP "RSD PTR ".
    // We can put it at 0x000F8000!
    
    // PAM0 is already unlocked by SMBIOS, 0xF0000-0xFFFFF is R/W RAM.
    struct acpi_rsdp* rsdp = (struct acpi_rsdp*)0x000F8000;
    struct acpi_rsdt* rsdt = (struct acpi_rsdt*)0x000F8040;
    
    for (int i=0; i<sizeof(struct acpi_rsdp); i++) ((u8*)rsdp)[i] = 0;
    for (int i=0; i<sizeof(struct acpi_rsdt); i++) ((u8*)rsdt)[i] = 0;
    
    rsdp->signature[0] = 'R'; rsdp->signature[1] = 'S'; rsdp->signature[2] = 'D'; rsdp->signature[3] = ' ';
    rsdp->signature[4] = 'P'; rsdp->signature[5] = 'T'; rsdp->signature[6] = 'R'; rsdp->signature[7] = ' ';
    
    rsdp->oem_id[0] = 'V'; rsdp->oem_id[1] = 'I'; rsdp->oem_id[2] = 'B'; 
    rsdp->oem_id[3] = 'E'; rsdp->oem_id[4] = ' '; rsdp->oem_id[5] = ' ';
    
    rsdp->revision = 0; // ACPI 1.0
    rsdp->rsdt_address = (u32)rsdt;
    
    rsdp->checksum = compute_checksum(rsdp, sizeof(struct acpi_rsdp));
    
    // RSDT
    rsdt->header.signature[0] = 'R'; rsdt->header.signature[1] = 'S'; 
    rsdt->header.signature[2] = 'D'; rsdt->header.signature[3] = 'T';
    rsdt->header.length = sizeof(struct acpi_header); // No entries for now
    rsdt->header.revision = 1;
    
    char* oem = "VIBE  ";
    for(int i=0; i<6; i++) rsdt->header.oem_id[i] = oem[i];
    
    char* tbl = "VIBEBIOS";
    for(int i=0; i<8; i++) rsdt->header.oem_table_id[i] = tbl[i];
    
    rsdt->header.oem_revision = 1;
    rsdt->header.creator_id = 0x45424956; // "VIBE"
    rsdt->header.creator_revision = 1;
    
    rsdt->header.checksum = compute_checksum(rsdt, rsdt->header.length);
}
