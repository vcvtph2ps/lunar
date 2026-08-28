#pragma once
#include <common/ldr/ldr.h>
#include <memory/vm.h>

typedef struct {
    /// @brief address of where we start execution
    uintptr_t executable_entry_point;

    /// @brief address of the entry point in the image
    /// @note this may not match executable_entry_point due to PT_INTERP
    uintptr_t image_entry_point;

    /// @brief offset of the image in memory from it's lowest_address (for DYN images)
    uintptr_t image_offset;

    /// @brief Number of program headers in the elf binary
    uint16_t program_header_num;

    /// @brief Size of each program header in the elf binary
    uint16_t program_header_entry_size;

    /// @brief Address of program header table
    uintptr_t program_header_table;

    /// @brief load base of the interperter (ld.so, or other); 0 if no interpreter
    uintptr_t interp_base;
} elf_loader_info_t;

/**
 * @brief Load elf file into memory
 * @param address_space target user address space
 * @param path Path to elf file
 * @param out_elf_loader_info information about the loaded elf file
 * @returns true if the file was loaded successfully, else false
 */
bool elf_load_file(vm_address_space_t* address_space, const vfs_path_t* path, elf_loader_info_t* out_elf_loader_info);

bool elf_is_elf_file(void* buffer, size_t size);

extern ldr_loader_t g_elf_loader;
