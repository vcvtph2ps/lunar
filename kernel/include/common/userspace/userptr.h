#pragma once

#include <memory/vm.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    vm_address_space_t* address_space;
    uintptr_t user_ptr;
    size_t size;
} userptr_t;

/// @note: all of these functions use the current process for the address space
userptr_t* userptr_create(uintptr_t user_pointer, size_t size);

/// @note: string must be null terimated and be smaller than max_size
/// @note: max_size includes '\0'
userptr_t* userptr_create_string(uintptr_t user_pointer, size_t max_size);

/// @note: array must end in a 0 value
/// @note: element_size must be 1, 2, 4 or 8
/// @note: resulting size is in bytes and excludes the terminating element
userptr_t* userptr_create_array(uintptr_t user_pointer, size_t element_size);

bool userptr_write(userptr_t* userptr, const void* buffer);
bool userptr_read(userptr_t* userptr, void* buffer);
