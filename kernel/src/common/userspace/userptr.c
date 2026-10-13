#include <common/sched/sched.h>
#include <common/userspace/userptr.h>
#include <lib/string.h>
#include <lib/types.h>
#include <memory/heap.h>
#include <memory/vm.h>
#include <stdint.h>
#include <uapi/limits.h>

userptr_t* userptr_create(uintptr_t user_pointer, size_t size) {
    userptr_t* userptr = heap_alloc(sizeof(userptr_t));

    if(user_pointer + size > MEMORY_USERSPACE_END) {
        return nullptr;
    }

    userptr->address_space = sched_arch_thread_current()->process->address_space;
    userptr->user_ptr = user_pointer;
    userptr->size = size;
    return userptr;
}

#define USERPTR_SCAN_CHUNK 128

static size_t userptr_measure_string(vm_address_space_t* as, uintptr_t addr, size_t max_size) {
    char buffer[USERPTR_SCAN_CHUNK];
    size_t length = 0;

    if(addr >= MEMORY_USERSPACE_END)
        return SIZE_MAX;

    size_t max = MEMORY_USERSPACE_END - addr;
    if(max > max_size)
        max = max_size;


    while(length < max) {
        uintptr_t cur = addr + length;
        size_t to_page_end = PAGE_SIZE_DEFAULT - (cur & (PAGE_SIZE_DEFAULT - 1));
        size_t readable = math_min(to_page_end, max - length);
        size_t chunk_size = math_min(sizeof(buffer), math_min(to_page_end, readable));

        if(!vm_copy_from(buffer, as, cur, chunk_size)) {
            return SIZE_MAX;
        }

        int index = string_find_first_instance(buffer, '\0', chunk_size);
        if(index >= 0) {
            return length + (size_t) index;
        }

        length += chunk_size;
    }

    return length;
}

userptr_t* userptr_create_string(uintptr_t user_pointer, size_t max_size) {
    assert(max_size < UAPI_LIMITS_ARG_MAX);

    size_t size = userptr_measure_string(sched_arch_thread_current()->process->address_space, user_pointer, max_size - 1);
    if(size == SIZE_MAX)
        return nullptr;

    userptr_t* userptr = heap_alloc(sizeof(userptr_t));
    userptr->user_ptr = user_pointer;
    userptr->address_space = sched_arch_thread_current()->process->address_space;
    userptr->size = size;
    return userptr;
}

static size_t userptr_measure_array(vm_address_space_t* as, uintptr_t addr, size_t element_size) {
    uint8_t buffer[USERPTR_SCAN_CHUNK];
    size_t length = 0;

    if(addr >= MEMORY_USERSPACE_END)
        return SIZE_MAX;

    size_t max = MEMORY_USERSPACE_END - addr;
    if(max > UAPI_LIMITS_ARG_MAX)
        max = UAPI_LIMITS_ARG_MAX;

    while(length + element_size <= max) {
        uintptr_t cur = addr + length;
        size_t to_page_end = PAGE_SIZE_DEFAULT - (cur & (PAGE_SIZE_DEFAULT - 1));
        size_t chunk_size = math_min(sizeof(buffer), math_min(to_page_end, max - length));

        chunk_size &= ~(element_size - 1);

        if(chunk_size < element_size)
            chunk_size = element_size;

        if(vm_copy_from(buffer, as, cur, chunk_size) != chunk_size)
            return SIZE_MAX;

        for(size_t element = 0; element < chunk_size; element += element_size) {
            bool is_zero = true;
            for(size_t byte = 0; byte < element_size; byte++) {
                if(buffer[element + byte] != 0) {
                    is_zero = false;
                    break;
                }
            }

            if(is_zero)
                return length + element;
        }

        length += chunk_size;
    }

    return SIZE_MAX;
}

userptr_t* userptr_create_array(uintptr_t user_pointer, size_t element_size) {
    if(element_size != 1 && element_size != 2 && element_size != 4 && element_size != 8)
        return nullptr;

    vm_address_space_t* address_space = sched_arch_thread_current()->process->address_space;
    size_t size = userptr_measure_array(address_space, user_pointer, element_size);
    if(size == SIZE_MAX)
        return nullptr;

    userptr_t* userptr = heap_alloc(sizeof(userptr_t));
    userptr->address_space = address_space;
    userptr->user_ptr = user_pointer;
    userptr->size = size;
    return userptr;
}

bool userptr_write(userptr_t* userptr, const void* buffer) {
    return vm_copy_to(userptr->address_space, userptr->user_ptr, buffer, userptr->size) == userptr->size;
}

bool userptr_read(userptr_t* userptr, void* buffer) {
    return vm_copy_from(buffer, userptr->address_space, userptr->user_ptr, userptr->size) == userptr->size;
}
