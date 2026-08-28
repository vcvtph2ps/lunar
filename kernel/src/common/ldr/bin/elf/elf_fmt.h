#pragma once

#include <stdint.h>

// NOLINTBEGIN

#define ELF_NIDENT 16

typedef struct {
    unsigned char ident[ELF_NIDENT];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t program_header_offset;
    uint64_t scetion_header_offset;
    uint32_t flags;
    uint16_t elf_header_size;
    uint16_t program_header_entry_size;
    uint16_t program_header_count;
    uint16_t section_header_entry_size;
    uint16_t section_header_count;
    uint16_t section_header_str_index;
} elf64_elf_header_t;

#define ELF_CLASS_IDX 4
#define ELF_CLASS_64_BIT 2

#define ELF_DATA_IDX 5
#define ELF_DATA_2LSB 1

#define ELF_TYPE_EXEC 2
#define ELF_TYPE_DYN 3

#define ELF_MACHINE_X86_64 62
#define ELF_MACHINE_RISCV 243

typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t file_size;
    uint64_t mem_size;
    uint64_t align;
} elf64_program_header_t;

#define ELF_PROG_TYPE_LOAD 1
#define ELF_PROG_TYPE_INTERP 3
#define ELF_PROG_TYPE_PHDR 6

#define ELF_PROG_FLAGS_EXECUTE (1 << 0)
#define ELF_PROG_FLAGS_WRITE (1 << 1)
#define ELF_PROG_FLAGS_READ (1 << 2)

// NOLINTEND
