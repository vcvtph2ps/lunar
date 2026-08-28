#pragma once
#include <common/fs/vfs.h>
#include <memory/vm.h>

typedef struct {
    int argc;
    int envc;
    const char** argv;
    const char** envp;
} ldr_process_load_info_t;

typedef struct {
    /// @brief Address the process should begin executing at.
    /// @note For dynamic executables this is the interpreter's entry point, not necessarily the entry point recorded in the executable's header.
    uintptr_t entry_point;
} ldr_image_info_t;

typedef struct {
    /// @brief Returns whether this loader can load the file, determined by the file's magic.
    bool (*can_load)(const void* buffer, size_t size);

    /// @brief Loads the file into the given address space.
    /// @param out_image generic information about the loaded image, used by the process setup code
    /// @param out_format_info format specific information required by load_abi.
    bool (*load)(vm_address_space_t* address_space, const vfs_path_t* path, ldr_image_info_t* out_image, void* out_format_info);

    /// @brief Builds the ABI specific stack frame (argv/envp/auxv) on top of *inout_user_stack.
    /// @param format_info The value written to *out_format_info by load().
    bool (*load_abi)(vm_address_space_t* address_space, uintptr_t* inout_user_stack, const ldr_process_load_info_t* load_info, const void* format_info);
} ldr_loader_t;

/**
 * @brief Loads an executable into the given address space and sets up its ABI stack frame.
 * @param address_space The target address space.
 * @param path Path to the executable to load.
 * @param load_info Command line / environment arguments for the process.
 * @param inout_user_stack On entry, the top of the userspace stack. On return, the final stack pointer for the new process.
 * @param entry_point Receives the address the process should start executing at.
 */
bool ldr_setup_process(vm_address_space_t* address_space, const vfs_path_t* path, const ldr_process_load_info_t* load_info, uintptr_t* inout_user_stack, uintptr_t* entry_point);
