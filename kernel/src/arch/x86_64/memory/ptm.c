#include <arch/x86_64/internal/cpuid.h>
#include <arch/x86_64/internal/cr.h>
#include <arch/x86_64/internal/msr.h>
#include <arch/x86_64/memory.h>
#include <common/arch.h>
#include <common/assert.h>
#include <common/init.h>
#include <common/interrupts/ipi.h>
#include <common/log.h>
#include <lib/helpers.h>
#include <memory/pagedb.h>
#include <memory/pmm.h>
#include <memory/pt_radix.h>
#include <memory/ptm.h>
#include <memory/vm.h>
#include <protocol/bootinfo.h>

bool g_arch_pte_la57_enabled = false;
bool g_pat_supported = false;
bool g_huge_pages_support = false;

extern vm_address_space_t* g_vm_global_address_space;

static bool map_page(arch_pte_entry_t* top_level_page_table, uintptr_t vaddr, uintptr_t paddr, page_size_t page_size, vm_protection_t prot, vm_cache_t cache, vm_privilege_t privilege, bool global, bool mmio) {
    uint32_t target_level = pte_page_size_to_level(page_size);

    pt_radix_walk_path_t path;
    pt_radix_walk_result_t res = pt_radix_walk(top_level_page_table, vaddr, target_level, true, true, prot, privilege, &path);
    if(res != PT_RADIX_WALK_OK) return false;

    pt_radix_walk_step_t* leaf = &path.steps[path.step_count - 1];
    arch_pte_entry_t* table = leaf->table;
    uint32_t index = leaf->index;
    uint32_t level = leaf->level;

    arch_pte_entry_t prev = table[index];
    arch_pte_entry_t entry = pte_make_leaf_entry(paddr, prot, cache, privilege, level, global);
    __atomic_store_n(&table[index], entry, __ATOMIC_SEQ_CST);

    if(!pte_is_present(prev)) {
        ATOMIC_LOAD_ADD(&pagedb_get_page(pt_radix_table_pfn(table))->page_level_mapping_count, 1, ATOMIC_SEQ_CST);

        if(!mmio) {
            uint64_t num_4k_pages = pte_level_page_size(level) / ARCH_PAGE_SIZE_4K;
            for(uint64_t i = 0; i < num_4k_pages; i++) pagedb_page_map(paddr / ARCH_PAGE_SIZE_4K + i);
        }
    }

    return true;
}

static bool internal_ptm_physical(arch_pte_entry_t* top_level_page_table, uintptr_t vaddr, uintptr_t* paddr) {
    pt_radix_walk_path_t path;
    pt_radix_walk_result_t res = pt_radix_walk(top_level_page_table, vaddr, 1, false, false, VM_PROT_RO, VM_PRIVILEGE_KERNEL, &path);
    if(res == PT_RADIX_WALK_NOMEM) return false;

    pt_radix_walk_step_t* last = &path.steps[path.step_count - 1];
    arch_pte_entry_t entry = last->table[last->index];
    uint32_t level = last->level;

    if(!pte_is_present(entry)) return false;

    *paddr = pte_get_leaf_phys(entry, level) | (vaddr & (pte_level_page_size(level) - 1u));
    return true;
}

void ptm_flush_tlb(virt_addr_t vaddr, size_t length) {
    for(; length > 0; length -= ARCH_PAGE_SIZE_4K, vaddr += ARCH_PAGE_SIZE_4K) asm volatile("invlpg (%0)" ::"r"(vaddr) : "memory");
}

bool ptm_map(vm_address_space_t* address_space, virt_addr_t vaddr, phys_addr_t paddr, size_t length, vm_protection_t prot, vm_cache_t cache, vm_privilege_t privilege, bool global, bool mmio) {
    assert(vaddr % ARCH_PAGE_SIZE_4K == 0);
    assert(paddr % ARCH_PAGE_SIZE_4K == 0);
    assert(length % ARCH_PAGE_SIZE_4K == 0);

    if(!prot.read) LOG_WARN("Mapping with no read permission is not supported, ignoring");
    spinlock_nodw_lock(&address_space->ptm.ptm_lock);

    arch_pte_entry_t* top = (arch_pte_entry_t*) PTM_TO_HHDM(address_space->ptm.top_level_page_table);
    for(size_t i = 0; i < length;) {
        page_size_t cursize = ARCH_PAGE_SIZE_4K;
        if(paddr % ARCH_PAGE_SIZE_2M == 0 && vaddr % ARCH_PAGE_SIZE_2M == 0 && length - i >= ARCH_PAGE_SIZE_2M) cursize = ARCH_PAGE_SIZE_2M;
        if(g_huge_pages_support && paddr % ARCH_PAGE_SIZE_1G == 0 && vaddr % ARCH_PAGE_SIZE_1G == 0 && length - i >= ARCH_PAGE_SIZE_1G) cursize = ARCH_PAGE_SIZE_1G;

        if(!map_page(top, vaddr + i, paddr + i, cursize, prot, cache, privilege, global, mmio)) {
            spinlock_nodw_unlock(&address_space->ptm.ptm_lock);
            return false;
        }
        i += cursize;
    }

    ptm_flush_tlb(vaddr, length);
    spinlock_nodw_unlock(&address_space->ptm.ptm_lock);
    return true;
}

bool ptm_rewrite(vm_address_space_t* address_space, uintptr_t vaddr, size_t length, vm_protection_t prot, vm_cache_t cache, vm_privilege_t privilege, bool global) {
    assert(vaddr % ARCH_PAGE_SIZE_4K == 0);
    assert(length % ARCH_PAGE_SIZE_4K == 0);

    if(!prot.read) LOG_FAIL("No-read mapping is not supported on x86_64\n");
    spinlock_nodw_lock(&address_space->ptm.ptm_lock);

    for(size_t i = 0; i < length;) {
        arch_pte_entry_t* top = (arch_pte_entry_t*) PTM_TO_HHDM(address_space->ptm.top_level_page_table);

        pt_radix_walk_path_t path;
        pt_radix_walk_result_t res = pt_radix_walk(top, vaddr + i, 1, false, false, prot, privilege, &path);
        if(res == PT_RADIX_WALK_NOT_FOUND) {
            i += ARCH_PAGE_SIZE_4K;
            continue;
        }

        pt_radix_walk_step_t* last = &path.steps[path.step_count - 1];
        arch_pte_entry_t* table = last->table;
        uint32_t index = last->index;
        uint32_t level = last->level;
        arch_pte_entry_t entry = table[index];

        if(pte_is_large(entry, level)) {
            size_t ps = pte_level_page_size(level);
            if((vaddr + i) % ps != 0 || length - i < ps) {
                entry = pt_radix_break_large(table, index, level);
                if(entry == 0) {
                    spinlock_nodw_unlock(&address_space->ptm.ptm_lock);
                    return false;
                }
                continue;
            }
        }

        arch_pte_entry_t new_entry = pte_rewrite_leaf_entry(entry, prot, cache, privilege, level, global);
        __atomic_store_n(&table[index], new_entry, __ATOMIC_SEQ_CST);

        i += pte_level_page_size(level);
    }

    ptm_flush_tlb(vaddr, length);
    ipi_broadcast((ipi_message_t) {
        .type = IPI_TLB_FLUSH,
        .tlb_flush = { .vaddr = vaddr, .length = length },
    });
    spinlock_nodw_unlock(&address_space->ptm.ptm_lock);
    return true;
}

void ptm_unmap(vm_address_space_t* address_space, uintptr_t vaddr, size_t length, bool mmio) {
    assert(vaddr % ARCH_PAGE_SIZE_4K == 0);
    assert(length % ARCH_PAGE_SIZE_4K == 0);

    spinlock_nodw_lock(&address_space->ptm.ptm_lock);

    for(size_t i = 0; i < length;) {
        arch_pte_entry_t* top = (arch_pte_entry_t*) PTM_TO_HHDM(address_space->ptm.top_level_page_table);
        pt_radix_walk_path_t path;

        pt_radix_walk_result_t res = pt_radix_walk(top, vaddr + i, 1, false, false, VM_PROT_RW, VM_PRIVILEGE_KERNEL, &path);
        if(res == PT_RADIX_WALK_NOT_FOUND) {
            i += ARCH_PAGE_SIZE_4K;
            continue;
        }

        pt_radix_walk_step_t* last = &path.steps[path.step_count - 1];
        arch_pte_entry_t* table = last->table;
        uint32_t index = last->index;
        uint32_t level = last->level;
        arch_pte_entry_t entry = table[index];

        if(!pte_is_present(entry)) {
            i += pte_level_page_size(level);
            continue;
        }

        if(pte_is_large(entry, level)) {
            size_t ps = pte_level_page_size(level);
            if((vaddr + i) % ps != 0 || length - i < ps) {
                entry = pt_radix_break_large(table, index, level);
                if(entry == 0) goto skip;
                continue;
            }
        }

        __atomic_store_n(&table[index], 0, __ATOMIC_SEQ_CST);

        if(!mmio) {
            uint64_t num_4k_pages = pte_level_page_size(level) / ARCH_PAGE_SIZE_4K;
            uintptr_t paddr_unmap = pte_get_leaf_phys(entry, level);
            for(uint64_t k = 0; k < num_4k_pages; k++) pagedb_page_unmap(paddr_unmap / ARCH_PAGE_SIZE_4K + k);
        }

        for(int d = (int) path.step_count - 2; d >= 0; d--) {
            pt_radix_walk_step_t* step = &path.steps[d];
            pt_radix_walk_step_t* child = &path.steps[d + 1];
            uint32_t old_count = ATOMIC_LOAD_SUB(&pagedb_get_page(pt_radix_table_pfn(child->table))->page_level_mapping_count, 1, ATOMIC_SEQ_CST);

            if(old_count != 1) break;

            __atomic_store_n(&step->table[step->index], 0, __ATOMIC_SEQ_CST);
            pmm_free_page((uintptr_t) PTM_FROM_HHDM(child->table));
        }

        i += pte_level_page_size(level);
        continue;

    skip:
        i += ARCH_PAGE_SIZE_4K;
    }

    ptm_flush_tlb(vaddr, length);
    ipi_broadcast((ipi_message_t) {
        .type = IPI_TLB_FLUSH,
        .tlb_flush = { .vaddr = vaddr, .length = length },
    });
    spinlock_nodw_unlock(&address_space->ptm.ptm_lock);
}

bool ptm_physical(vm_address_space_t* address_space, uintptr_t vaddr, uintptr_t* paddr) {
    spinlock_nodw_lock(&address_space->ptm.ptm_lock);

    arch_pte_entry_t* top = (arch_pte_entry_t*) PTM_TO_HHDM(address_space->ptm.top_level_page_table);
    bool result = internal_ptm_physical(top, vaddr, paddr);

    spinlock_nodw_unlock(&address_space->ptm.ptm_lock);
    return result;
}

void ptm_load_address_space(vm_address_space_t* address_space) {
    arch_cr_write_cr3(address_space->ptm.top_level_page_table);
}

static void map_kernel() {
    for(size_t i = 0; i < g_init_boot_info->kernel_segment_count; i++) {
        bootinfo_segment_t* seg = &g_init_boot_info->kernel_segments[i];

        vm_protection_t prot = {
            .read = (seg->flags & BOOTINFO_SEGMENT_FLAG_READ) != 0,
            .write = (seg->flags & BOOTINFO_SEGMENT_FLAG_WRITE) != 0,
            .execute = (seg->flags & BOOTINFO_SEGMENT_FLAG_EXECUTE) != 0,
        };

        const phys_addr_t aligned_paddr = ALIGN_DOWN(seg->paddr, ARCH_PAGE_SIZE_4K);
        const virt_addr_t aligned_vaddr = ALIGN_DOWN(seg->vaddr, ARCH_PAGE_SIZE_4K);
        const size_t align_diff = seg->paddr - aligned_paddr;
        const size_t aligned_length = ALIGN_UP(seg->size + align_diff, ARCH_PAGE_SIZE_4K);

        ptm_map(g_vm_global_address_space, aligned_vaddr, aligned_paddr, aligned_length, prot, VM_CACHE_NORMAL, VM_PRIVILEGE_KERNEL, true, false);
    }

    for(size_t i = 0; i < g_init_boot_info->mm_entry_count; i++) {
        bootinfo_mm_entry_t* entry = &g_init_boot_info->mm_entries[i];
        if(entry->type == BOOTINFO_MM_TYPE_BAD) continue;

        const phys_addr_t aligned_paddr = ALIGN_DOWN(entry->phys_base, ARCH_PAGE_SIZE_4K);
        const virt_addr_t aligned_vaddr = (virt_addr_t) PTM_TO_HHDM(aligned_paddr);
        const size_t align_diff = entry->phys_base - aligned_paddr;
        const size_t aligned_length = ALIGN_UP(entry->length + align_diff, ARCH_PAGE_SIZE_4K);

        ptm_map(g_vm_global_address_space, aligned_vaddr, aligned_paddr, aligned_length, VM_PROT_RW, VM_CACHE_NORMAL, VM_PRIVILEGE_KERNEL, true, false);
    }

    arch_pte_entry_t* boot_top = (arch_pte_entry_t*) PTM_TO_HHDM(arch_cr_read_cr3() & ARCH_PTE_ADDR_MASK);

    for(uintptr_t va = g_init_boot_info->pfndb_start; va < g_init_boot_info->pfndb_start + g_init_boot_info->pfndb_size; va += ARCH_PAGE_SIZE_4K) {
        uintptr_t pa;
        if(internal_ptm_physical(boot_top, va, &pa)) ptm_map(g_vm_global_address_space, va, pa, ARCH_PAGE_SIZE_4K, VM_PROT_RW, VM_CACHE_NORMAL, VM_PRIVILEGE_KERNEL, true, false);
    }
}

void ptm_init_kernel(uint32_t core_id) {
    if(!INIT_CORE_IS_BSP(core_id)) {
        ptm_load_address_space(g_vm_global_address_space);
        return;
    }

    g_vm_global_address_space = (void*) PTM_TO_HHDM(pmm_alloc_page(PMM_FLAG_ZERO | PMM_FLAG_PANIC));
    g_vm_global_address_space->ptm.top_level_page_table = pmm_alloc_page(PMM_FLAG_ZERO | PMM_FLAG_PANIC);
    g_vm_global_address_space->ptm.ptm_lock = SPINLOCK_NO_DW_INIT;
    g_vm_global_address_space->lock = SPINLOCK_NO_DW_INIT;
    g_vm_global_address_space->regions_tree = vm_create_regions();
    g_vm_global_address_space->start = MEMORY_KERNELSPACE_START;
    g_vm_global_address_space->end = MEMORY_KERNELSPACE_END;

    g_pat_supported = arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_PAT);
    g_huge_pages_support = arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_PDPE1GB_PAGES);
    g_arch_pte_la57_enabled = (arch_cr_read_cr4() & (1u << 12)) != 0; /* CR4.LA57 */

    arch_pte_entry_t* pml4 = (arch_pte_entry_t*) PTM_TO_HHDM(g_vm_global_address_space->ptm.top_level_page_table);
    for(int i = 256; i < 512; i++) { pml4[i] = ARCH_PTE_PRESENT | ARCH_PTE_RW | (pmm_alloc_page(PMM_FLAG_ZERO | PMM_FLAG_PANIC) & ARCH_PTE_ADDR_MASK); }

    map_kernel();

    log_framebuffer_enable(false);
    ptm_load_address_space(g_vm_global_address_space);
    LOG_OKAY("switched to kernel page tables :33\n");
    vm_init_kernel();

    for(size_t i = 0; i < g_init_boot_info->framebuffer_count; i++) {
        bootinfo_framebuffer_t framebuffer = g_init_boot_info->framebuffers[i];
        const phys_addr_t paddr = framebuffer.paddr;
        const phys_addr_t aligned_paddr = ALIGN_DOWN(paddr, ARCH_PAGE_SIZE_4K);
        const virt_addr_t align_diff = paddr - aligned_paddr;
        const size_t aligned_length = ALIGN_UP(framebuffer.size + align_diff, ARCH_PAGE_SIZE_4K);

        const void* new_vaddr = vm_map_direct(g_vm_global_address_space, VM_NO_HINT, aligned_length, VM_PROT_RW, VM_CACHE_WRITE_COMBINE, aligned_paddr, VM_FLAG_MMIO);
        LOG_INFO("mapping framebuffer %zu, 0x%lx, 0x%lx, 0x%lx, 0x%lx, %018lx\n", i, paddr, aligned_paddr, align_diff, aligned_length, (uintptr_t) new_vaddr);
        g_init_boot_info->framebuffers[i].vaddr = (void*) ((uintptr_t) new_vaddr + align_diff);
    }
    log_framebuffer_reinit();
    log_framebuffer_enable(true);
}

bool ptm_init_user(vm_address_space_t* address_space) {
    address_space->ptm.top_level_page_table = pmm_alloc_page(PMM_FLAG_ZERO);
    if(address_space->ptm.top_level_page_table == 0) return false;

    address_space->ptm.ptm_lock = SPINLOCK_NO_DW_INIT;
    address_space->is_user = true;
    address_space->lock = SPINLOCK_NO_DW_INIT;
    address_space->regions_tree = vm_create_regions();
    address_space->start = MEMORY_USERSPACE_START;
    address_space->end = MEMORY_USERSPACE_END;

    arch_pte_entry_t* user_pml4 = (arch_pte_entry_t*) PTM_TO_HHDM(address_space->ptm.top_level_page_table);
    arch_pte_entry_t* kernel_pml4 = (arch_pte_entry_t*) PTM_TO_HHDM(g_vm_global_address_space->ptm.top_level_page_table);
    for(int i = 256; i < 512; i++) user_pml4[i] = kernel_pml4[i];

    return true;
}
