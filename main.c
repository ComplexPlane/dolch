#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>

#define MAX_SECTIONS 18
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
    printf("DOL size: %#010zx\n", header->dol_size);

    for (int i = 0; i < MAX_SECTIONS; i++) {
        uint32_t address = header->section_addresses[i];
        uint32_t offset = header->section_offsets[i];
        uint32_t size = header->section_sizes[i];

        if (address == 0) {
            printf("Section %02d: unused.\n", i);
        } else {
            printf("Section %02d: offset = %#010x, start_addr = %#010x, "
                   "end_addr = %#010x, size = %#010x\n",
                   i,
                   offset,
                   address,
                   address + size,
                   size);
        }
    }

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

void add_section_to_header(struct dol_header *header) {
/* TODO
 * Search for last section in memory
 * Search for last section offset in file
 * Search for unused section ID to represent new section
 * Use 16-byte alignment of section just to be extra safe
 * Mutate header, return new section number
 * Another function can be responsible for writing the new header and section to the file
 */

    // Find a memory address that appears after all mapped sections that we can place a new
    // section into
    uint32_t free_addr = 0;
    for (int section = 0; section < MAX_SECTIONS; section++) {
        uint32_t end_addr = header->section_addresses[section] + header->section_sizes[section];
        if (end_addr != 0 && end_addr > free_addr) {
            free_addr = end_addr;
        }
    }
    uint32_t bss_end = header->bss_address + header->bss_size;
    if (bss_end > free_addr) free_addr = bss_end;

    // Find a section ID we can place it into
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: <in.dol> <out.dol> <space_size>\n");
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

    struct dol_header header = {};
    if (!read_dol_header(in_dol, &header)) {
        fprintf(stderr, "Failed to read DOL file header: %s\n", in_dol);
        exit(1);
    }

    print_dol_header(&header);
}
