#pragma once
#include <common/ldr/ldr.h>
#include <memory/vm.h>
#include <stdbool.h>

/**
 * @brief Builds the System V ABI userspace stack frame (argc, argv, envp, auxv) on top of
 *        *inout_user_stack.
 * @param format_info The elf_loader_info_t produced by the elf loader's load().
 */
bool sysv_load_abi(vm_address_space_t* address_space, uintptr_t* inout_user_stack, const ldr_process_load_info_t* load_info, const void* format_info);
