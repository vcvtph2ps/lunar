#pragma once
#include <stdint.h>

#define PMM_FLAG_NONE (0)
#define PMM_FLAG_ZERO (1 << 0)
#define PMM_FLAG_PANIC (1 << 1)

typedef uint8_t pmm_flag_t;

/**
 * @brief Initializes the physical memory manager by populating the allocator with the usable memory regions provided by the prekernel.
 */
void pmm_init();

/**
 * @brief Allocates a single page of physical memory and returns its address.
 * @param flags A bitfield of pmm_flag_t values that modify the behavior of the allocation. The following flags are defined:
 * - PMM_FLAG_ZERO: If set, the allocated page will be zeroed out before being returned.
 * - PMM_FLAG_PANIC: If set, the function will panic if no free pages are available. If not set, the function will return 0 to indicate failure.
 * @return The physical address of the allocated page, or 0 if no free pages are available and PMM_FLAG_PANIC is not set.
 */
[[nodiscard]] uintptr_t pmm_alloc_page(pmm_flag_t flags);

/**
 * @brief Frees a single page of physical memory at the given address, returning it to the allocator.
 * @note If the page's reference count is greater than 0, the reference count will be decremented but the page will not be returned to the allocator.
 * @param addr The physical address of the page to free. Must be aligned to the page
 */
void pmm_free_page(uintptr_t addr);
