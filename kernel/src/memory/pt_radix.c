#include <common/assert.h>
#include <lib/helpers.h>
#include <memory/pagedb.h>
#include <memory/pmm.h>
#include <memory/pt_radix.h>

arch_pte_entry_t pt_radix_break_large(arch_pte_entry_t* table, uint32_t index, uint32_t level) {
    assert(level > 1u);

    arch_pte_entry_t entry = table[index];

    phys_addr_t base_phys = pte_get_leaf_phys(entry, level);

    uint32_t child_level = level - 1u;
    size_t child_page_size = pte_level_page_size(child_level);

    uintptr_t child_table_phys = pmm_alloc_page(PMM_FLAG_ZERO);
    if(child_table_phys == 0) return 0;

    arch_pte_entry_t* child_table = (arch_pte_entry_t*) PTM_TO_HHDM(child_table_phys);
    for(int i = 0; i < 512; i++) {
        phys_addr_t child_phys = base_phys + (phys_addr_t) i * child_page_size;
        child_table[i] = pte_make_large_child(entry, child_phys, child_level);
    }

    ATOMIC_STORE(&pagedb_get_page(child_table_phys / ARCH_PAGE_SIZE_4K)->page_level_mapping_count, 512, ATOMIC_RELAXED);

    arch_pte_entry_t parent_entry = pte_make_table_from_large(entry, child_table_phys);
    __atomic_store_n(&table[index], parent_entry, __ATOMIC_SEQ_CST);

    return parent_entry;
}

pt_radix_walk_result_t pt_radix_walk(arch_pte_entry_t* top_level_table, virt_addr_t vaddr, uint32_t target_level, bool allocate, bool split_large, vm_protection_t prot, vm_privilege_t privilege, pt_radix_walk_path_t* path) {
    arch_pte_entry_t* current_table = top_level_table;
    path->step_count = 0;

    for(uint32_t level = pte_top_level(); level > target_level; level--) {
        uint32_t index = pte_index(vaddr, level);
        path->steps[path->step_count++] = (pt_radix_walk_step_t) {
            .table = current_table,
            .index = index,
            .level = level,
        };

        arch_pte_entry_t entry = current_table[index];

        if(!pte_is_present(entry)) {
            if(!allocate) return PT_RADIX_WALK_NOT_FOUND;

            pmm_flag_t flags = PMM_FLAG_ZERO | (privilege == VM_PRIVILEGE_KERNEL ? PMM_FLAG_PANIC : 0);
            uintptr_t page = pmm_alloc_page(flags);
            if(page == 0) return PT_RADIX_WALK_NOMEM;

            entry = pte_make_new_table((phys_addr_t) page, prot, privilege);

            ATOMIC_LOAD_ADD(&pagedb_get_page(pt_radix_table_pfn(current_table))->page_level_mapping_count, 1, ATOMIC_SEQ_CST);
        } else if(pte_is_large(entry, level)) {
            if(!split_large) { return PT_RADIX_WALK_OK; }
            entry = pt_radix_break_large(current_table, index, level);
            if(entry == 0) return PT_RADIX_WALK_NOMEM;
        }

        if(allocate) {
            entry = pte_widen_table_entry(entry, prot, privilege);
            __atomic_store_n(&current_table[index], entry, __ATOMIC_SEQ_CST);
        }

        current_table = (arch_pte_entry_t*) PTM_TO_HHDM(pte_get_table_phys(entry));
    }

    uint32_t leaf_index = pte_index(vaddr, target_level);
    path->steps[path->step_count++] = (pt_radix_walk_step_t) {
        .table = current_table,
        .index = leaf_index,
        .level = target_level,
    };

    return PT_RADIX_WALK_OK;
}
