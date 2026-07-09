#pragma once
#include <common/memory.h>
#include <stddef.h>
#include <stdint.h>

// NOLINTBEGIN
#define IS_POWER_OF_TWO(x) ((x) != 0 && ((x) & ((x) - 1)) == 0)
#define ALIGN_UP(x, align) ((((uintptr_t) (x)) + ((align) - 1)) & ~((uintptr_t) ((align) - 1)))
#define ALIGN_DOWN(x, align) (((uintptr_t) (x)) & ~((uintptr_t) ((align) - 1)))

typedef uintptr_t phys_addr_t;
typedef uintptr_t virt_addr_t;

typedef enum {
    PAGE_SIZE_DEFAULT = ARCH_PAGE_SIZE_DEFAULT,
    PAGE_SIZE_SMALL = ARCH_PAGE_SIZE_4K,
    PAGE_SIZE_LARGE = ARCH_PAGE_SIZE_2M,
    PAGE_SIZE_HUGE = ARCH_PAGE_SIZE_1G,
} page_size_t;

#define MEMORY_KERNELSPACE_START 0xFFFF'8000'0000'0000
#define MEMORY_KERNELSPACE_END ((uintptr_t) -PAGE_SIZE_DEFAULT)
#define MEMORY_USERSPACE_START (PAGE_SIZE_DEFAULT)
#define MEMORY_USERSPACE_END (((uintptr_t) 1 << 47) - PAGE_SIZE_DEFAULT)

// NOLINTEND
