#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <memory.h>

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

uint32_t parse_u32_bigendian(uint32_t val) {
    uint8_t *ptr = (void *) &val;

    return (uint32_t) ptr[0] << 24u |
           (uint32_t) ptr[1] << 16u |
           (uint32_t) ptr[2] << 8u |
           (uint32_t) ptr[3] << 0u;
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

bool read_dol_header(const char *filename, struct dol_header *header) {
    FILE *dol_file = fopen(filename, "rb");
    if (!dol_file) {
        fprintf(stderr, "Failed to open file: %s\n", filename);
        return false;
    }

    fseek(dol_file, 0, SEEK_END);
    size_t dol_size = ftell(dol_file);
    rewind(dol_file);

    uint8_t header_buf[HEADER_SIZE] = {};
    fread(header_buf, HEADER_SIZE, 1, dol_file);
    if (ferror(dol_file)) {
        fprintf(stderr, "Failed to read DOL file header into memory: %s\n", filename);
        fclose(dol_file);
        return false;
    }

    header->dol_size = dol_size;
    parse_dol_header(header_buf, header);

    return true;
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

bool parse_size(const char *str, size_t *size) {
    long size_long = strtol(str, NULL, 10);
    if (errno == ERANGE || size_long <= 0) {
        return false;
    }
    *size = (size_t) size_long;
    return true;
}

uint32_t align_16(uint32_t addr) {
    uint32_t aligned = (addr / 16) * 16;
    if (aligned < addr) aligned += 16;
    return aligned;
}

bool add_section_to_header(struct dol_header *in_header,
                           uint32_t section_size,
                           struct dol_header *out_header,
                           int *new_section_id) {

    // Find a memory address that appears after all mapped sections that we can place a new
    // section into
    uint32_t free_addr = 0;
    for (int section = 0; section < MAX_SECTIONS; section++) {
        uint32_t end_addr = in_header->section_addresses[section] + in_header->section_sizes[section];
        if (end_addr != 0 && end_addr > free_addr) {
            free_addr = end_addr;
        }
    }
    uint32_t bss_end = in_header->bss_address + in_header->bss_size;
    if (bss_end > free_addr) free_addr = bss_end;

    // Find a text section ID we can place it into
    int free_text_id = -1;
    for (int section = 0; section < TEXT_SECTIONS; section++) {
        if (in_header->section_addresses[section] == 0) {
            free_text_id = section;
            break;
        }
    }
    if (free_text_id == -1) {
        fprintf(stderr, "No free text sections available in DOL file\n");
        return false;
    }

    // Create new header with new section
    memcpy(out_header, in_header, sizeof(struct dol_header));
    out_header->section_addresses[free_text_id] = align_16(free_addr);
    out_header->section_offsets[free_text_id] = align_16(out_header->dol_size);
    out_header->section_sizes[free_text_id] = align_16(section_size);
    out_header->dol_size = out_header->section_offsets[free_text_id] + out_header->section_sizes[free_text_id];

    if (new_section_id) *new_section_id = free_text_id;

    return true;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <in.dol> <out.dol> <space_size>\n", argv[0]);
        exit(1);
    }

    const char *in_dol = argv[1];
    const char *out_dol = argv[2];
    const char *space_size_str = argv[3];
    size_t space_size = 0;
    if (!parse_size(space_size_str, &space_size)) {
        fprintf(stderr, "Invalid space size: %s\n", space_size_str);
        exit(1);
    }

    struct dol_header orig_header = {};
    if (!read_dol_header(in_dol, &orig_header)) {
        fprintf(stderr, "Failed to read DOL file header: %s\n", in_dol);
        exit(1);
    }

    printf("Original header:\n");
    print_dol_header(&orig_header);
    printf("\n");

    struct dol_header new_header = {};
    int new_section_id = 0;
    if (!add_section_to_header(&orig_header, space_size, &new_header, &new_section_id)) {
        fprintf(stderr, "Failed to add a new section.\n");
        exit(1);
    }

    printf("New header:\n");
    print_dol_header(&new_header);
    printf("\n");
}
