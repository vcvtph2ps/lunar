#pragma once
#include <common/memory.h>
#include <memory/pte.h>
#include <memory/ptm.h>
#include <memory/vm.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PT_RADIX_WALK_OK,
    PT_RADIX_WALK_NOMEM,
    PT_RADIX_WALK_NOT_FOUND,
} pt_radix_walk_result_t;

typedef struct {
    arch_pte_entry_t* table;
    uint32_t index;
    uint32_t level;
} pt_radix_walk_step_t;

typedef struct {
    pt_radix_walk_step_t steps[ARCH_PTE_MAX_LEVELS + 1];
    uint32_t step_count;
} pt_radix_walk_path_t;

static inline uint64_t pt_radix_table_pfn(const arch_pte_entry_t* table) {
    return (uintptr_t) PTM_FROM_HHDM(table) / ARCH_PAGE_SIZE_4K;
}

/**
 * @brief Walk the radix tree
 *
 * @param top_level_table hhdm pointer to the top level page table.
 * @param vaddr virtual address to walk for.
 * @param target_level level at which to stop (if the walk reaches a leaf at a higher level and split_large is false, the walk stops early).
 * @param allocate if true, missing intermediate tables are allocated.
 * @param split_large if true, large page entries encountered above target_level are split into smaller pages. if false, the walk stops early and returns
 * @param prot protection hints for intermediate entry flag widening.
 * @param privilege privilege hints for intermediate entry flag widening.
 * @param[out] path filled with one step per level visited.
 */
pt_radix_walk_result_t pt_radix_walk(arch_pte_entry_t* top_level_table, virt_addr_t vaddr, uint32_t target_level, bool allocate, bool split_large, vm_protection_t prot, vm_privilege_t privilege, pt_radix_walk_path_t* path);

/**
 * @brief Break a large page entry into smaller child entries
 *
 * @param table hhdm pointer to the page-table page containing the entry.
 * @param index index of the large-page entry within table.
 * @param level level of table (must be > 1).
 *
 * @return the new parent entry (pointing to the child table), or 0 if failed.
 */
arch_pte_entry_t pt_radix_break_large(arch_pte_entry_t* table, uint32_t index, uint32_t level);
