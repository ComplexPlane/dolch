#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <memory.h>
#include <stdarg.h>

#define TEXT_SECTIONS 7
#define DATA_SECTIONS 11
#define MAX_SECTIONS (TEXT_SECTIONS + DATA_SECTIONS)
#define HEADER_SIZE ((MAX_SECTIONS * 3 + 3) * 4)

struct dol_header {
    size_t dol_size; // Metadata, not part of actual header

    uint32_t section_offsets[MAX_SECTIONS];
    uint32_t section_addresses[MAX_SECTIONS];
    uint32_t section_sizes[MAX_SECTIONS];
    uint32_t bss_address;
    uint32_t bss_size;
    uint32_t entry_point_address;
};

void fatal(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    exit(1);
}

uint32_t parse_u32_bigendian(uint32_t val) {
    uint8_t *ptr = (void *) &val;

    return (uint32_t) ptr[0] << 24u |
           (uint32_t) ptr[1] << 16u |
           (uint32_t) ptr[2] << 8u |
           (uint32_t) ptr[3] << 0u;
}

uint32_t encode_u32_bigendian(uint32_t val) {
    uint32_t ret = 0;
    uint8_t *ptr = (void *) &ret;

    ptr[0] |= val >> 24u;
    ptr[1] |= val >> 16u;
    ptr[2] |= val >> 8u;
    ptr[3] |= val >> 0u;

    return ret;
}

void parse_dol_header(void *dolbuf, struct dol_header *header) {
    uint32_t *dol_u32_arr = dolbuf;

    for (int i = 0; i < MAX_SECTIONS; i++) {
        header->section_offsets[i] = parse_u32_bigendian(dol_u32_arr[i]);
        header->section_addresses[i] = parse_u32_bigendian(dol_u32_arr[MAX_SECTIONS + i]);
        header->section_sizes[i] = parse_u32_bigendian(dol_u32_arr[2 * MAX_SECTIONS + i]);
    }

    header->bss_address = parse_u32_bigendian(dol_u32_arr[3 * MAX_SECTIONS]);
    header->bss_size = parse_u32_bigendian(dol_u32_arr[3 * MAX_SECTIONS + 1]);
    header->entry_point_address = parse_u32_bigendian(dol_u32_arr[3 * MAX_SECTIONS + 2]);
}

void read_dol_header(FILE *dol_file, struct dol_header *header) {
    fseek(dol_file, 0, SEEK_END);
    size_t dol_size = ftell(dol_file);
    rewind(dol_file);

    uint8_t header_buf[HEADER_SIZE] = {};
    fread(header_buf, HEADER_SIZE, 1, dol_file);
    if (ferror(dol_file)) {
        fatal("Failed to read DOL file header into memory\n");
    }

    header->dol_size = dol_size;
    parse_dol_header(header_buf, header);
}

void print_dol_header(struct dol_header *header) {
    printf("DOL size: %#010zx\n\n", header->dol_size);

    for (int i = 0; i < MAX_SECTIONS; i++) {
        if (i == 0) {
            printf("TEXT SECTIONS\n");
        } else if (i == TEXT_SECTIONS) {
            printf("\nDATA SECTIONS\n");
        }

        uint32_t address = header->section_addresses[i];
        uint32_t offset = header->section_offsets[i];
        uint32_t size = header->section_sizes[i];

        if (address == 0) {
            printf("Section %02d: unused.\n", i);
        } else {
            printf("Section %02d: start_offset = %#010x, end_offset = %#010x, "
                   "start_addr = %#010x, end_addr = %#010x, size = %#010x\n",
                   i,
                   offset,
                   offset + size,
                   address,
                   address + size,
                   size);
        }
    }
    printf("\n");

    printf("bss: start_addr = %#010x, end_addr = %#010x, size = %#010x\n",
            header->bss_address,
            header->bss_address + header->bss_size,
            header->bss_size);
    printf("entry point address: %#010x\n", header->entry_point_address);
}

bool parse_size(const char *str, uint32_t *size) {
    uint32_t size_long = strtoul(str, NULL, 0);
    if (errno == ERANGE || size_long <= 0) {
        return false;
    }
    *size = size_long;
    return true;
}

uint32_t align_16(uint32_t addr) {
    uint32_t aligned = (addr / 16) * 16;
    if (aligned < addr) aligned += 16;
    return aligned;
}

void add_section_to_header(struct dol_header *in_header,
                           uint32_t section_size,
                           uint32_t *section_addr,

                           struct dol_header *out_header,
                           int *new_section_id) {

    uint32_t free_start = 0;
    uint32_t bss_end = in_header->bss_address + in_header->bss_size;

    if (section_addr) {
        // Check if section is available
        free_start = *section_addr;
        uint32_t free_end = free_start + section_size;
        for (int section = 0; section < MAX_SECTIONS; section++) {
            uint32_t section_start = in_header->section_addresses[section];
            uint32_t section_end = section_start + in_header->section_sizes[section];
            if (free_end >= section_start && free_start < section_end) {
                fatal("Requested new section overlaps section %d\n", section);
            }
        }
        if (!(free_end <= in_header->bss_address || free_start >= bss_end)) {
            fatal("Requested new section overlaps bss section\n");
        }

    } else {
        // Find a memory address that appears after all mapped sections that we can place a new
        // section into
        for (int section = 0; section < MAX_SECTIONS; section++) {
            uint32_t end_addr = in_header->section_addresses[section] + in_header->section_sizes[section];
            if (end_addr != 0 && end_addr > free_start) {
                free_start = end_addr;
            }
        }
        if (bss_end > free_start) free_start = bss_end;
    }

    // Find a text section ID we can place it into
    int free_text_id = -1;
    for (int section = 0; section < TEXT_SECTIONS; section++) {
        if (in_header->section_addresses[section] == 0) {
            free_text_id = section;
            break;
        }
    }
    if (free_text_id == -1) {
        fatal("No free text sections available in DOL file\n");
    }

    // Create new header with new section
    memcpy(out_header, in_header, sizeof(struct dol_header));
    out_header->section_addresses[free_text_id] = align_16(free_start);
    out_header->section_offsets[free_text_id] = align_16(out_header->dol_size);
    out_header->section_sizes[free_text_id] = align_16(section_size);
    out_header->dol_size = out_header->section_offsets[free_text_id] + out_header->section_sizes[free_text_id];

    if (new_section_id) *new_section_id = free_text_id;
}

void fwrite4_bigendian(FILE *file, uint32_t val) {
    uint32_t val_bigendian = encode_u32_bigendian(val);
    fwrite(&val_bigendian, 4, 1, file);
}

void write_dol_header(FILE *dolfile, struct dol_header *header) {
    rewind(dolfile);

    for (int section = 0; section < MAX_SECTIONS; section++) {
        fwrite4_bigendian(dolfile, header->section_offsets[section]);
    }
    for (int section = 0; section < MAX_SECTIONS; section++) {
        fwrite4_bigendian(dolfile, header->section_addresses[section]);
    }
    for (int section = 0; section < MAX_SECTIONS; section++) {
        fwrite4_bigendian(dolfile, header->section_sizes[section]);
    }

    fwrite4_bigendian(dolfile, header->bss_address);
    fwrite4_bigendian(dolfile, header->bss_size);
    fwrite4_bigendian(dolfile, header->entry_point_address);
}

void add_section_to_dol(FILE *dol_file, struct dol_header *new_header) {
    write_dol_header(dol_file, new_header);

    // Add section to end of file
    fseek(dol_file, 0, SEEK_END);

    // Better way to do this without malloc?
    uint32_t additional_size = new_header->dol_size - ftell(dol_file);
    void *buf = calloc(1, additional_size);
    fwrite(buf, additional_size, 1, dol_file);
    free(buf);
}

void cp(FILE *file1, FILE *file2) {
    fseek(file1, 0, SEEK_END);
    size_t file1_size = ftell(file1);
    rewind(file1);
    rewind(file2);

    void *buf = calloc(1, file1_size);
    fread(buf, file1_size, 1, file1);
    fwrite(buf, file1_size, 1, file2);

    free(buf);
}

void usage() {
    fprintf(stderr, "dolch: a simple Gamecube/Wii DOL injection and inspection utility.\n\n");
    fprintf(stderr, "USAGE:\n");
    fprintf(stderr, "    dolch addsection <IN_DOL> <OUT_DOL> <SECTION_SIZE> [SECTION_ADDRESS]\n");
    fprintf(stderr, "    dolch info <IN_DOL>\n");
    fprintf(stderr, "    dolch offset <IN_DOL> <ADDRESS>\n");
    exit(1);
}

FILE *open_dol_file(const char *path, const char *modes) {
    FILE *f = fopen(path, modes);
    if (!f) {
        fatal("Failed to open DOL file: %s\n", path);
    }
    return f;
}

void cmd_add_section(int argc, char **argv) {
    if (argc != 5 && argc != 6) usage();

    const char *in_dol_path = argv[2];
    const char *out_dol_path = argv[3];
    const char *section_size_str = argv[4];
    const char *section_addr_str = argc == 6 ? argv[5] : NULL;

    uint32_t section_size = 0;
    uint32_t section_addr = 0;
    if (!parse_size(section_size_str, &section_size)) {
        fatal("Invalid space size: %s\n", section_size_str);
    }
    if (section_addr_str) {
        if (!parse_size(section_addr_str, &section_addr)) {
            fatal("Invalid section address: %s\n", section_addr_str);
        }
    }

    // Open input and output files
    FILE *in_dol_file = open_dol_file(in_dol_path, "rb");
    remove(out_dol_path);
    FILE *out_dol_file = open_dol_file(out_dol_path, "wb");

    // Generate new DOL header
    struct dol_header orig_header = {}, new_header = {};
    int new_section_id = 0;
    read_dol_header(in_dol_file, &orig_header);
    add_section_to_header(&orig_header,
                          section_size,
                          section_addr_str ? &section_addr : NULL,
                          &new_header,
                          &new_section_id);

    // Write new DOL header to output file
    cp(in_dol_file, out_dol_file);
    add_section_to_dol(out_dol_file, &new_header);

    printf("Added section %d in %s, wrote to %s.\n", new_section_id, in_dol_path, out_dol_path);

    fclose(in_dol_file);
    fclose(out_dol_file);
}

void cmd_info(int argc, char **argv) {
    if (argc != 3) usage();

    FILE *in_dol_file = open_dol_file(argv[2], "rb");
    struct dol_header header = {};
    read_dol_header(in_dol_file, &header);
    print_dol_header(&header);

    fclose(in_dol_file);
}

void cmd_address_to_offset(int argc, char **argv) {
    if (argc != 4) usage();
    const char *dol_filepath = argv[2];
    const char *addr_str = argv[3];

    FILE *dol_file = open_dol_file(dol_filepath, "rb");
    struct dol_header header = {};
    read_dol_header(dol_file, &header);

    uint32_t addr = strtoul(addr_str, NULL, 0);
    if (errno == ERANGE || addr == 0) {
        fatal("Invalid address: %s\n", addr_str);
    }

    int addr_section = -1;
    for (int section = 0; section < MAX_SECTIONS; section++) {
        uint32_t start = header.section_addresses[section];
        uint32_t end = start + header.section_sizes[section];
        if (addr >= start && addr < end) {
            addr_section = section;
            break;
        }
    }
    if (addr_section == -1) {
        fatal("Address %s is not part of any section in %s.\n", addr_str, dol_filepath);
    }

    uint32_t offset = addr - header.section_addresses[addr_section] + header
            .section_offsets[addr_section];
    printf("Memory address %s is at offset %#010x.\n", addr_str, offset);
}

int main(int argc, char **argv) {
    // Parse arguments
    if (argc < 2) {
        usage();
    }

    const char *subcmd = argv[1];

    if (!strcmp(subcmd, "addsection")) cmd_add_section(argc, argv);
    else if (!strcmp(subcmd, "info")) cmd_info(argc, argv);
    else if (!strcmp(subcmd, "offset")) cmd_address_to_offset(argc, argv);
    else usage();
}
