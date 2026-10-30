#pragma once
#include <common/sync/spinlock.h>
#include <stddef.h>
#include <stdint.h>

#define PMM_FLAG_NONE (0)
#define PMM_FLAG_ZERO (1 << 0) // If set, the allocated page will be zeroed out before being returned.
#define PMM_FLAG_PANIC (1 << 1) // If set, the function will panic if no free pages are available. If not set, the function will return 0 to indicate failure.

typedef uint8_t pmm_flag_t;

typedef struct {
    size_t num_pages;
    uint64_t free_list_pfn;
    spinlock_no_dpc_t pmm_lock;
} pmm_t;

/**
 * @brief Initializes the physical memory manager by populating the allocator with the usable memory regions provided by the prekernel.
 */
void pmm_init(uint32_t core_id);

/**
 * @brief Allocates a single page of physical memory and returns its address.
 * @param flags A bitfield of pmm_flag_t values that modify the behavior of the allocation.
 * @return The physical address of the allocated page, or 0 if no free pages are available and PMM_FLAG_PANIC is not set.
 */
[[nodiscard]] uintptr_t pmm_alloc_page(pmm_flag_t flags);

/**
 * @brief Allocates a single page of physical memory from the low (<1 MiB) freelist.
 * @param flags A bitfield of pmm_flag_t values that modify the behavior of the allocation.
 * @return The physical address of the allocated page, or 0 if no free pages are available and PMM_FLAG_PANIC is not set.
 * @note This memory is only intended for the real mode application processor trampoline.
 */
[[nodiscard]] uintptr_t pmm_alloc_page_low(pmm_flag_t flags);

/**
 * @brief Frees a single page of physical memory at the given address, returning it to the allocator.
 * @note If the page's reference count is greater than 0, the reference count will be decremented but the page will not be returned to the allocator.
 * @param addr The physical address of the page to free. Must be aligned to the page
 */
void pmm_free_page(uintptr_t addr);

/**
 * @brief Reclaims a region of physical memory, adding it to the allocator
 * @note this function ignores all pfndb state, do not use unless the page is being reclaimed from the bootloader, but the pages do need vaild pfndb entries
 * @param addr The physical address to start of the region to reclaim. Must be aligned to the page
 * @param size The size of the region to reclaim. Must be aligned to the page
 */
void pmm_reclaim(uintptr_t addr, size_t size);
