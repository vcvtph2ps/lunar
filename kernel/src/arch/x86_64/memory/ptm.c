#include <arch/x86_64/internal/cpuid.h>
#include <arch/x86_64/internal/cr.h>
#include <arch/x86_64/internal/msr.h>
#include <arch/x86_64/interrupts/interrupt.h>
#include <arch/x86_64/memory.h>
#include <arch/x86_64/memory/pte.h>
#include <common/arch.h>
#include <common/assert.h>
#include <common/init.h>
#include <common/interrupts/ipi.h>
#include <common/log.h>
#include <common/sync/spinlock.h>
#include <common/userspace/userspace.h>
#include <lib/helpers.h>
#include <lib/math.h>
#include <lib/string.h>
#include <lib/types.h>
#include <memory/pagedb.h>
#include <memory/pmm.h>
#include <memory/pt_radix.h>
#include <memory/ptm.h>
#include <memory/vm.h>
#include <protocol/bootinfo.h>

bool g_arch_pte_la57_enabled = false;
static bool g_pat_supported = false;
static bool g_huge_pages_support = false;

extern vm_address_space_t* g_vm_global_address_space;

static bool map_page(arch_pte_entry_t* top_level_page_table, uintptr_t vaddr, uintptr_t paddr, page_size_t page_size, vm_protection_t prot, vm_cache_t cache, vm_privilege_t privilege, bool global, bool no_pagedb) {
    uint32_t target_level = pte_page_size_to_level(page_size);

    pt_radix_walk_path_t path;
    pt_radix_walk_result_t res = pt_radix_walk(top_level_page_table, vaddr, target_level, true, true, prot, privilege, &path);
    if(res != PT_RADIX_WALK_OK)
        return false;

    pt_radix_walk_step_t* leaf = &path.steps[path.step_count - 1];
    arch_pte_entry_t* table = leaf->table;
    uint32_t index = leaf->index;
    uint32_t level = leaf->level;

    arch_pte_entry_t prev = table[index];
    arch_pte_entry_t entry = pte_make_leaf_entry(paddr, prot, cache, privilege, level, global);
    ATOMIC_STORE(&table[index], entry, ATOMIC_SEQ_CST);

    if(!pte_is_present(prev)) {
        ATOMIC_LOAD_ADD(&pagedb_get_page(pt_radix_table_pfn(table))->allocated.page_level_mapping_count, 1, ATOMIC_SEQ_CST);

        if(!no_pagedb) {
            uint64_t num_4k_pages = pte_level_page_size(level) / ARCH_PAGE_SIZE_4K;
            for(uint64_t i = 0; i < num_4k_pages; i++) pagedb_page_map((paddr >> 12) + i);
        }
    }

    return true;
}

static bool internal_ptm_physical(arch_pte_entry_t* top_level_page_table, uintptr_t vaddr, uintptr_t* paddr) {
    pt_radix_walk_path_t path;
    pt_radix_walk_result_t res = pt_radix_walk(top_level_page_table, vaddr, 1, false, false, VM_PROT_RO, VM_PRIVILEGE_KERNEL, &path);
    if(res == PT_RADIX_WALK_NOMEM)
        return false;

    pt_radix_walk_step_t* last = &path.steps[path.step_count - 1];
    arch_pte_entry_t entry = last->table[last->index];
    uint32_t level = last->level;

    if(!pte_is_present(entry))
        return false;

    *paddr = pte_get_leaf_phys(entry, level) | (vaddr & (pte_level_page_size(level) - 1u));
    return true;
}

void ptm_flush_tlb(virt_addr_t vaddr, size_t length) {
    if(length >= 64 * ARCH_PAGE_SIZE_4K) {
        ptm_flush_tlb_all();
        return;
    }
    for(; length > 0; length -= ARCH_PAGE_SIZE_4K, vaddr += ARCH_PAGE_SIZE_4K) asm volatile("invlpg (%0)" ::"r"(vaddr) : "memory");
}

void ptm_flush_tlb_all(void) {
    arch_cr_write_cr3(arch_cr_read_cr3());
}

bool ptm_map(vm_address_space_t* address_space, virt_addr_t vaddr, phys_addr_t paddr, size_t length, vm_protection_t prot, vm_cache_t cache, vm_privilege_t privilege, bool global, bool no_pagedb) {
    assert(vaddr % ARCH_PAGE_SIZE_4K == 0);
    assert(paddr % ARCH_PAGE_SIZE_4K == 0);
    assert(length % ARCH_PAGE_SIZE_4K == 0);

    if(!prot.read)
        LOG_WARN("Mapping with no read permission is not supported, ignoring\n");
    spinlock_nodw_lock(&address_space->ptm.ptm_lock);

    arch_pte_entry_t* top = (arch_pte_entry_t*) PTM_TO_HHDM(address_space->ptm.top_level_page_table);
    for(size_t i = 0; i < length;) {
        page_size_t cursize = ARCH_PAGE_SIZE_4K;
        if(paddr % ARCH_PAGE_SIZE_2M == 0 && vaddr % ARCH_PAGE_SIZE_2M == 0 && length - i >= ARCH_PAGE_SIZE_2M)
            cursize = ARCH_PAGE_SIZE_2M;
        if(g_huge_pages_support && paddr % ARCH_PAGE_SIZE_1G == 0 && vaddr % ARCH_PAGE_SIZE_1G == 0 && length - i >= ARCH_PAGE_SIZE_1G)
            cursize = ARCH_PAGE_SIZE_1G;

        if(!map_page(top, vaddr + i, paddr + i, cursize, prot, cache, privilege, global, no_pagedb)) {
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

    if(!prot.read)
        LOG_FAIL("No-read mapping is not supported on x86_64\n");
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

        // Widen intermediate entries.
        for(uint32_t step = 0; step + 1 < path.step_count; step++) {
            arch_pte_entry_t* itable = path.steps[step].table;
            arch_pte_entry_t ientry = itable[path.steps[step].index];
            itable[path.steps[step].index] = pte_widen_table_entry(ientry, prot, privilege);
        }

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
        ATOMIC_STORE(&table[index], new_entry, ATOMIC_SEQ_CST);

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

void ptm_unmap(vm_address_space_t* address_space, uintptr_t vaddr, size_t length, bool no_pagedb) {
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
                if(entry == 0)
                    goto skip;
                continue;
            }
        }

        ATOMIC_STORE(&table[index], 0, ATOMIC_SEQ_CST);

        if(!no_pagedb) {
            uint64_t num_4k_pages = pte_level_page_size(level) / ARCH_PAGE_SIZE_4K;
            uintptr_t paddr_unmap = pte_get_leaf_phys(entry, level);
            for(uint64_t k = 0; k < num_4k_pages; k++) pagedb_page_unmap((paddr_unmap >> 12) + k);
        }

        for(int d = (int) path.step_count - 2; d >= 0; d--) {
            pt_radix_walk_step_t* step = &path.steps[d];
            pt_radix_walk_step_t* child = &path.steps[d + 1];
            uint32_t old_count = ATOMIC_LOAD_SUB(&pagedb_get_page(pt_radix_table_pfn(child->table))->allocated.page_level_mapping_count, 1, ATOMIC_SEQ_CST);

            if(old_count != 1)
                break;

            ATOMIC_STORE(&step->table[step->index], 0, ATOMIC_SEQ_CST);
            pmm_free_page((uintptr_t) PTM_FROM_HHDM(child->table));
        }

        i += pte_level_page_size(level);
        continue;

    skip:
        i += pte_level_page_size(level);
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
    // @note: slightly cursed, we pass no_pagedb=true to all of these to bypass the pagedb
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

        ptm_map(g_vm_global_address_space, aligned_vaddr, aligned_paddr, aligned_length, prot, VM_CACHE_NORMAL, VM_PRIVILEGE_KERNEL, true, true);
    }

    for(size_t i = 0; i < g_init_boot_info->mm_entry_count; i++) {
        bootinfo_mm_entry_t* entry = &g_init_boot_info->mm_entries[i];
        if(entry->type == BOOTINFO_MM_TYPE_RESERVED || entry->type == BOOTINFO_MM_TYPE_BAD)
            continue;

        const phys_addr_t aligned_paddr = ALIGN_DOWN(entry->phys_base, ARCH_PAGE_SIZE_4K);
        const virt_addr_t aligned_vaddr = (virt_addr_t) PTM_TO_HHDM(aligned_paddr);
        const size_t align_diff = entry->phys_base - aligned_paddr;
        const size_t aligned_length = ALIGN_UP(entry->length + align_diff, ARCH_PAGE_SIZE_4K);

        ptm_map(g_vm_global_address_space, aligned_vaddr, aligned_paddr, aligned_length, VM_PROT_RW, VM_CACHE_NORMAL, VM_PRIVILEGE_KERNEL, true, true);
    }

    arch_pte_entry_t* boot_top = (arch_pte_entry_t*) PTM_TO_HHDM(arch_cr_read_cr3() & ARCH_PTE_ADDR_MASK);

    for(uintptr_t offset = 0; offset < g_init_boot_info->pfndb_bitmap_size; offset += ARCH_PAGE_SIZE_4K) {
        uintptr_t va = g_init_boot_info->pfndb_bitmap_start + offset;
        uintptr_t pa;
        bool mapped = internal_ptm_physical(boot_top, va, &pa);
        assert(mapped);
        ptm_map(g_vm_global_address_space, va, pa, ARCH_PAGE_SIZE_4K, VM_PROT_RO, VM_CACHE_NORMAL, VM_PRIVILEGE_KERNEL, true, true);
    }

    for(uintptr_t va = g_init_boot_info->pfndb_start; va < g_init_boot_info->pfndb_start + g_init_boot_info->pfndb_size; va += ARCH_PAGE_SIZE_4K) {
        uintptr_t pa;
        if(internal_ptm_physical(boot_top, va, &pa))
            ptm_map(g_vm_global_address_space, va, pa, ARCH_PAGE_SIZE_4K, VM_PROT_RW, VM_CACHE_NORMAL, VM_PRIVILEGE_KERNEL, true, true);
    }
}

static void page_fault_handler(arch_interrupt_frame_t* frame, void* ctx) {
    (void) ctx;
    if(!frame->is_user) {
        arch_panic_int(frame);
    }
    vm_fault_reason_t reason = VM_FAULT_UKKNOWN;
    if((frame->error & (1 << 0)) == 0) {
        reason = VM_FAULT_NOT_PRESENT;
    } else if((frame->error & (1 << 1)) != 0) {
        reason = VM_FAULT_WRITE_TO_RO;
    }
    LOG_UTRC("addr=0x%016lx, type=%s\n", frame->interrupt_data, vm_fault_reason_to_string(reason));
    if(!vm_fault(frame->interrupt_data, reason)) {
        // @todo: SIGSEGV
        user_assert(false && "SIGSEGV unimplemented");
        return;
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
    for(int i = 256; i < 512; i++) {
        pml4[i] = ARCH_PTE_PRESENT | ARCH_PTE_RW | (pmm_alloc_page(PMM_FLAG_ZERO | PMM_FLAG_PANIC) & ARCH_PTE_ADDR_MASK);
    }

    map_kernel();

    ptm_load_address_space(g_vm_global_address_space);
    LOG_OKAY("switched to kernel page tables :33\n");
    vm_init_kernel();

    for(size_t i = 0; i < g_init_boot_info->framebuffer_count; i++) {
        bootinfo_framebuffer_t framebuffer = g_init_boot_info->framebuffers[i];
        const phys_addr_t paddr = framebuffer.paddr;
        const phys_addr_t aligned_paddr = ALIGN_DOWN(paddr, ARCH_PAGE_SIZE_4K);
        const phys_addr_t align_diff = paddr - aligned_paddr;
        const size_t aligned_length = ALIGN_UP(framebuffer.size + align_diff, ARCH_PAGE_SIZE_4K);

        const void* new_vaddr = vm_map_direct(g_vm_global_address_space, VM_NO_HINT, aligned_length, VM_PROT_RW, VM_CACHE_WRITE_COMBINE, aligned_paddr, VM_FLAG_NO_PAGEDB);
        LOG_INFO("mapping framebuffer %zu, 0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx\n", i, paddr, aligned_paddr, align_diff, aligned_length, (uintptr_t) new_vaddr);
        g_init_boot_info->framebuffers[i].vaddr = (void*) ((uintptr_t) new_vaddr + align_diff);
    }

    interrupt_set_hardirq_handler(0x0E, page_fault_handler, nullptr);
}

bool ptm_init_user(vm_address_space_t* address_space) {
    address_space->ptm.top_level_page_table = pmm_alloc_page(PMM_FLAG_ZERO);
    if(address_space->ptm.top_level_page_table == 0)
        return false;

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

static bool ptm_make_cow_page_table(arch_pte_entry_t* new_page_table_base, arch_pte_entry_t* old_page_table_base, uint32_t level) {
    int entry_count = level == pte_top_level() ? 256 : 512;
    for(int i = 0; i < entry_count; i++) {
        if(!pte_is_present(old_page_table_base[i])) {
            continue;
        }

        if(pte_is_leaf(old_page_table_base[i], level)) {
            uint64_t num_4k = pte_level_page_size(level) / ARCH_PAGE_SIZE_4K;
            uint64_t pfn_base = pte_get_leaf_phys(old_page_table_base[i], level) / ARCH_PAGE_SIZE_4K;

            new_page_table_base[i] = old_page_table_base[i];
            new_page_table_base[i] &= ~ARCH_PTE_RW;
            old_page_table_base[i] &= ~ARCH_PTE_RW;

            for(uint64_t j = 0; j < num_4k; j++) {
                pagedb_page_map(pfn_base + j);
            }
            continue;
        }

        uintptr_t new_pt_level = pmm_alloc_page(PMM_FLAG_ZERO);
        if(new_pt_level == 0)
            return false;
        new_page_table_base[i] = pte_clone_table(new_pt_level, old_page_table_base[i]);
        bool success = ptm_make_cow_page_table((arch_pte_entry_t*) PTM_TO_HHDM(new_pt_level), (arch_pte_entry_t*) PTM_TO_HHDM(pte_get_table_phys(old_page_table_base[i])), level - 1);
        if(!success) {
            return false;
        }
    }

    return true;
}

bool ptm_cow_fault(vm_address_space_t* address_space, uintptr_t vaddr, vm_protection_t prot, vm_cache_t cache, bool global) {
    virt_addr_t page_addr = ALIGN_DOWN(vaddr, ARCH_PAGE_SIZE_4K);

    uintptr_t phys;
    if(!ptm_physical(address_space, page_addr, &phys))
        return false;

    uint64_t pfn = phys >> 12;
    pagedb_page_t* page = pagedb_get_page(pfn);
    if(page == nullptr)
        return false;

    uint32_t map_count = ATOMIC_LOAD(&page->allocated.map_count, ATOMIC_RELAXED);
    vm_privilege_t priv = global ? VM_PRIVILEGE_KERNEL : VM_PRIVILEGE_USER;

    LOG_UTRC("cow fault vaddr=0x%lx, map_count=%u\n", vaddr, map_count);
    if(map_count == 1) {
        return ptm_rewrite(address_space, page_addr, ARCH_PAGE_SIZE_4K, prot, cache, priv, global);
    }

    uintptr_t new_phys = pmm_alloc_page(PMM_FLAG_NONE);
    if(new_phys == 0)
        return false;

    memory_copy((void*) PTM_TO_HHDM(new_phys), (void*) PTM_TO_HHDM(phys), ARCH_PAGE_SIZE_4K);

    ptm_unmap(address_space, page_addr, ARCH_PAGE_SIZE_4K, false);

    if(!ptm_map(address_space, page_addr, new_phys, ARCH_PAGE_SIZE_4K, prot, cache, priv, global, false)) {
        pmm_free_page(new_phys);
        return false;
    }

    return true;
}

bool ptm_fork_user(vm_address_space_t* new_as, vm_address_space_t* parent_as) {
    new_as->ptm.top_level_page_table = pmm_alloc_page(PMM_FLAG_ZERO);
    if(new_as->ptm.top_level_page_table == 0)
        return false;

    new_as->ptm.ptm_lock = SPINLOCK_NO_DW_INIT;

    spinlock_nodw_lock(&parent_as->ptm.ptm_lock);
    bool success = ptm_make_cow_page_table((arch_pte_entry_t*) PTM_TO_HHDM(new_as->ptm.top_level_page_table), (arch_pte_entry_t*) PTM_TO_HHDM(parent_as->ptm.top_level_page_table), pte_top_level());
    if(!success) {
        arch_panic("ptm: failed to fork page tables\n");
    }
    spinlock_nodw_unlock(&parent_as->ptm.ptm_lock);

    arch_pte_entry_t* user_pml4 = (arch_pte_entry_t*) PTM_TO_HHDM(new_as->ptm.top_level_page_table);
    arch_pte_entry_t* kernel_pml4 = (arch_pte_entry_t*) PTM_TO_HHDM(g_vm_global_address_space->ptm.top_level_page_table);
    for(int i = 256; i < 512; i++) user_pml4[i] = kernel_pml4[i];

    // @note: ugh this sucks
    ptm_flush_tlb_all();
    ipi_broadcast((ipi_message_t) {
        .type = IPI_TLB_FLUSH_ALL,
    });

    return true;
}
