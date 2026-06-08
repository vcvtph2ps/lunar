#include <arch/internal/cpuid.h>
#include <arch/internal/cr.h>
#include <arch/internal/msr.h>
#include <arch/memory.h>
#include <common/arch.h>
#include <common/assert.h>
#include <common/init.h>
#include <common/log.h>
#include <memory/pagedb.h>
#include <memory/pmm.h>
#include <memory/ptm.h>
#include <memory/vm.h>


#define VADDR_TO_INDEX(VADDR, LEVEL) (((VADDR) >> ((LEVEL) * 9 + 3)) & 0x1FF)
#define INDEX_TO_VADDR(INDEX, LEVEL) ((uint64_t) (INDEX) << ((LEVEL) * 9 + 3))
#define LEVEL_TO_PAGESIZE(LEVEL) (1UL << (12 + 9 * ((LEVEL) - 1)))

#define PAGE_BIT_PRESENT (1 << 0)
#define PAGE_BIT_RW (1 << 1)
#define PAGE_BIT_USER (1 << 2)
#define PAGE_BIT_WRITETHROUGH (1 << 3)
#define PAGE_BIT_DISABLECACHE (1 << 4)
#define PAGE_BIT_ACCESSED (1 << 5)
#define PAGE_BIT_GLOBAL (1 << 8)
#define PAGE_BIT_NX ((uint64_t) 1 << 63)
#define PAGE_BIT_PAT(PAGE_SIZE) ((PAGE_SIZE) == ARCH_PAGE_SIZE_4K ? SMALL_PAGE_BIT_PAT : HUGE_PAGE_BIT_PAT)
#define PAGE_ADDRESS_MASK(PAGE_SIZE) ((PAGE_SIZE) == ARCH_PAGE_SIZE_4K ? SMALL_PAGE_ADDRESS_MASK : HUGE_PAGE_ADDRESS_MASK)

#define SMALL_PAGE_BIT_PAT (1 << 7)
#define SMALL_PAGE_ADDRESS_MASK ((uint64_t) 0x000F'FFFF'FFFF'F000)

#define HUGE_PAGE_BIT_PAGE_STOP (1 << 7)
#define HUGE_PAGE_BIT_PAT (1 << 12)
#define HUGE_PAGE_ADDRESS_MASK ((uint64_t) 0x000F'FFFF'FFFF'0000)

#define PAT0 (0)
#define PAT1 (PAGE_BIT_WRITETHROUGH)
#define PAT2 (PAGE_BIT_DISABLECACHE)
#define PAT3 (PAGE_BIT_DISABLECACHE | PAGE_BIT_WRITETHROUGH)
#define PAT4(PAT_FLAG) (PAT_FLAG)
#define PAT5(PAT_FLAG) ((PAT_FLAG) | PAGE_BIT_WRITETHROUGH)
#define PAT6(PAT_FLAG) ((PAT_FLAG) | PAGE_BIT_DISABLECACHE)
#define PAT7(PAT_FLAG) ((PAT_FLAG) | PAGE_BIT_DISABLECACHE | PAGE_BIT_WRITETHROUGH)


/**
 * @brief Converts the given vm_privilege_t to the corresponding x86 page table flags.
 * @note This function assumes that the caller will only pass valid vm_privilege_t values, and does not perform any validation on the input. Passing an invalid value will result in undefined behavior.
 * @param privilege The vm_privilege_t value to convert
 * @return The corresponding x86 page table flags for the given privilege level
 */
static inline uint64_t privilege_to_x86_flags(vm_privilege_t privilege) {
    switch(privilege) {
        case VM_PRIVILEGE_KERNEL: return 0;
        case VM_PRIVILEGE_USER:   return PAGE_BIT_USER;
    }
    __builtin_unreachable();
}

/**
 * @brief Converts the given vm_cache_t to the corresponding x86 page table flags.
 * @note This function assumes that the caller will only pass valid vm_cache_t values, and does not perform any validation on the input. Passing an invalid value will result in undefined behavior.
 * @param cache The vm_cache_t value to convert
 * @param page_size The page size for which the cache flags are being converted
 * @return The corresponding x86 page table flags for the given cache type
 */
static inline uint64_t cache_to_x86_flags(vm_cache_t cache, page_size_t page_size) {
    switch(cache) {
        case VM_CACHE_NORMAL:        return PAT0;
        case VM_CACHE_WRITE_COMBINE: return PAT4(PAGE_BIT_PAT(page_size));
        case VM_CACHE_DISABLE:       return PAT3;
        case VM_CACHE_WRITE_THROUGH: return 0;
    }
    __builtin_unreachable();
}


extern vm_address_space_t* g_vm_global_address_space;
static vm_region_t g_kernel_region;
static vm_region_t g_hhdm_region;

static bool g_pat_supported = false;
static bool g_huge_pages_support = false;
static bool g_la57_enabled = false;

#define LEVEL_COUNT (g_la57_enabled ? 5 : 4)

/**
 * @brief Returns the physical frame number for a page table given its HHDM-mapped pointer.
 */
static inline uint64_t table_to_pfn(const uint64_t* table) {
    return (uintptr_t) PTM_FROM_HHDM(table) / ARCH_PAGE_SIZE_4K;
}

/**
 * @brief Breaks a large page (2MB or 1GB) into smaller pages by allocating a new page table and populating it with entries that map the appropriate portions of the original large page. The original page table entry is then updated to point to the
 * new page table, and the HUGE_PAGE_BIT_PAGE_STOP bit is cleared to indicate that it is no longer a large page.
 * @note This function assumes that the caller has already verified that the entry at the given index is present and is a large page. Calling this function on an entry that is not a large page will result in undefined behavior.
 * @param table The page table containing the entry to break
 * @param index The index of the entry to break within the table
 * @param current_level The current level of the page table hierarchy. This is used to determine the size of the pages being created and the appropriate flags to set in the new entries.
 * @return The new page table entry that was created to replace the original large page entry. This entry will have the same physical address as the original entry, but will point to the new page table instead of being a large page itself.
 */
static uint64_t break_large_page(uint64_t* table, int index, int current_level) {
    assert(current_level > 0);

    uint64_t entry = table[index];

    uintptr_t address = entry & HUGE_PAGE_ADDRESS_MASK;
    bool pat = entry & HUGE_PAGE_BIT_PAT;
    entry &= ~SMALL_PAGE_ADDRESS_MASK;

    uint64_t new_entry = entry;
    if(current_level - 1 == 0) {
        new_entry &= ~SMALL_PAGE_BIT_PAT;
        if(pat) new_entry |= SMALL_PAGE_BIT_PAT;
    } else {
        new_entry |= HUGE_PAGE_BIT_PAT;
    }

    uintptr_t page = pmm_alloc_page(PMM_FLAG_ZERO | ((entry & PAGE_BIT_USER) ? 0 : PMM_FLAG_PANIC));
    if(page == 0) return 0;

    entry |= page;
    if(pat) entry |= SMALL_PAGE_BIT_PAT;

    uint64_t* new_table = (void*) PTM_TO_HHDM(entry & SMALL_PAGE_ADDRESS_MASK);
    for(int i = 0; i < 512; i++) new_table[i] = new_entry | (address + i * ARCH_PAGE_SIZE_4K);

    ATOMIC_STORE(&pagedb_get_page(table_to_pfn(new_table))->page_level_mapping_count, (uint16_t) 512, __ATOMIC_RELAXED);

    __atomic_store(&table[index], &entry, __ATOMIC_SEQ_CST);

    return entry;
}

/**
 * @brief Maps a single page (of the specified size) at the given virtual address to the given physical address with the specified protection, cache, and privilege flags. This function will walk the page table hierarchy as needed to create any
 * necessary intermediate page tables, and will break large pages if necessary to ensure that the final mapping is created with the correct page size.
 * @note This function assumes that the caller has already verified that the virtual and physical addresses are properly aligned for the specified page size, and that the page size is valid. Calling this function with invalid parameters will result
 * in undefined behavior.
 * @param top_level_page_table The top-level page table to start the mapping from. This should be the page table for the address space being mapped into.
 * @param vaddr The virtual address to map. Must be aligned to the specified page size.
 * @param paddr The physical address to map to. Must be aligned to the specified page size.
 * @param page_size The size of the page to map. This determines how many levels of the page table hierarchy will be used for the mapping, and what flags will be set in the page table entries.
 * @param prot The protection flags for the mapping, which determine the read/write/execute permissions for the mapped page.
 * @param cache The cache flags for the mapping, which determine the caching behavior for the mapped page.
 * @param privilege The privilege level for the mapping, which determines whether the page is accessible from user mode or only from kernel mode.
 * @param global Whether the mapping should be marked as global, and not flushed from the TLB on a context switch.
 * @return true if the mapping was successful, false if it failed due to out of memory
 */
static bool map_page(uint64_t* top_level_page_table, uintptr_t vaddr, uintptr_t paddr, page_size_t page_size, vm_protection_t prot, vm_cache_t cache, vm_privilege_t privilege, bool global) {
    int lowest_index;
    switch(page_size) {
        case ARCH_PAGE_SIZE_4K: lowest_index = 1; break;
        case ARCH_PAGE_SIZE_2M: lowest_index = 2; break;
        case ARCH_PAGE_SIZE_1G: lowest_index = 3; break;
    }

    uint64_t* current_table = top_level_page_table;
    for(int j = LEVEL_COUNT; j > lowest_index; j--) {
        int index = VADDR_TO_INDEX(vaddr, j);

        uint64_t entry = current_table[index];
        if((entry & PAGE_BIT_PRESENT) == 0) {
            uintptr_t page = pmm_alloc_page(PMM_FLAG_ZERO | (privilege == VM_PRIVILEGE_KERNEL ? PMM_FLAG_PANIC : 0));
            if(page == 0) return false;
            entry = PAGE_BIT_PRESENT | (page & SMALL_PAGE_ADDRESS_MASK);
            if(!prot.execute) entry |= PAGE_BIT_NX;
            ATOMIC_LOAD_ADD(&pagedb_get_page(table_to_pfn(current_table))->page_level_mapping_count, (uint16_t) 1, __ATOMIC_SEQ_CST);
        } else {
            if((entry & HUGE_PAGE_BIT_PAGE_STOP) != 0) {
                entry = break_large_page(current_table, index, j);
                if(entry == 0) return false;
            }
            if(prot.execute) entry &= ~PAGE_BIT_NX;
        }
        if(prot.write) entry |= PAGE_BIT_RW;
        entry |= privilege_to_x86_flags(privilege);
        __atomic_store(&current_table[index], &entry, __ATOMIC_SEQ_CST);

        current_table = (uint64_t*) PTM_TO_HHDM(entry & SMALL_PAGE_ADDRESS_MASK);
    }

    int leaf_index = VADDR_TO_INDEX(vaddr, lowest_index);
    uint64_t prev_leaf = current_table[leaf_index];

    uint64_t entry = PAGE_BIT_PRESENT | (paddr & PAGE_ADDRESS_MASK(page_size)) | privilege_to_x86_flags(privilege) | cache_to_x86_flags(cache, page_size);
    if(page_size != ARCH_PAGE_SIZE_4K) entry |= HUGE_PAGE_BIT_PAGE_STOP;
    if(prot.write) entry |= PAGE_BIT_RW;
    if(!prot.execute) entry |= PAGE_BIT_NX;
    if(global) entry |= PAGE_BIT_GLOBAL;
    __atomic_store(&current_table[leaf_index], &entry, __ATOMIC_SEQ_CST);

    if(!(prev_leaf & PAGE_BIT_PRESENT)) { ATOMIC_LOAD_ADD(&pagedb_get_page(table_to_pfn(current_table))->page_level_mapping_count, (uint16_t) 1, __ATOMIC_SEQ_CST); }

    return true;
}

void ptm_flush_tlb(virt_addr_t vaddr, size_t length) {
    for(; length > 0; length -= PAGE_SIZE_DEFAULT, vaddr += PAGE_SIZE_DEFAULT) asm volatile("invlpg (%0)" ::"r"(vaddr) : "memory");
}

bool ptm_map(vm_address_space_t* address_space, virt_addr_t vaddr, phys_addr_t paddr, size_t length, vm_protection_t prot, vm_cache_t cache, vm_privilege_t privilege, bool global) {
    assert(vaddr % ARCH_PAGE_SIZE_4K == 0);
    assert(paddr % ARCH_PAGE_SIZE_4K == 0);
    assert(length % ARCH_PAGE_SIZE_4K == 0);

    if(!prot.read) LOG_WARN("Mapping with no read permission is not supported, ignoring");
    spinlock_nodw_lock(&address_space->ptm.ptm_lock);

    for(size_t i = 0; i < length;) {
        page_size_t cursize = ARCH_PAGE_SIZE_4K;
        if(paddr % ARCH_PAGE_SIZE_2M == 0 && vaddr % ARCH_PAGE_SIZE_2M == 0 && length - i >= ARCH_PAGE_SIZE_2M) cursize = ARCH_PAGE_SIZE_2M;
        if(g_huge_pages_support && paddr % ARCH_PAGE_SIZE_1G == 0 && vaddr % ARCH_PAGE_SIZE_1G == 0 && length - i >= ARCH_PAGE_SIZE_1G) cursize = ARCH_PAGE_SIZE_1G;
        if(!map_page((uint64_t*) PTM_TO_HHDM(address_space->ptm.top_level_page_table), vaddr + i, paddr + i, cursize, prot, cache, privilege, global)) {
            spinlock_nodw_unlock(&address_space->ptm.ptm_lock);
            return false;
        }
        i += cursize;
    }

    ptm_flush_tlb(vaddr, length);
    // @todo: ipi
    spinlock_nodw_unlock(&address_space->ptm.ptm_lock);
    return true;
}

bool ptm_rewrite(vm_address_space_t* address_space, uintptr_t vaddr, size_t length, vm_protection_t prot, vm_cache_t cache, vm_privilege_t privilege, bool global) {
    assert(vaddr % ARCH_PAGE_SIZE_4K == 0);
    assert(length % ARCH_PAGE_SIZE_4K == 0);

    if(!prot.read) LOG_FAIL("No-read mapping is not supported on x86_64\n");
    spinlock_nodw_lock(&address_space->ptm.ptm_lock);

    for(size_t i = 0; i < length;) {
        uint64_t* current_table = (uint64_t*) PTM_TO_HHDM(address_space->ptm.top_level_page_table);

        int j = LEVEL_COUNT;
        for(; j > 1; j--) {
            int index = VADDR_TO_INDEX(vaddr + i, j);
            uint64_t entry = current_table[index];

            if((entry & PAGE_BIT_PRESENT) == 0) goto skip;
            if((entry & HUGE_PAGE_BIT_PAGE_STOP) != 0) {
                assert(j <= 3);
                if(INDEX_TO_VADDR(index, j) < vaddr + i || LEVEL_TO_PAGESIZE(j) > length - i) {
                    entry = break_large_page(current_table, index, j);
                    if(entry == 0) {
                        spinlock_nodw_unlock(&address_space->ptm.ptm_lock);
                        return false;
                    }
                } else {
                    break;
                }
            }

            if(prot.write) entry |= PAGE_BIT_RW;
            if(prot.execute) entry &= ~PAGE_BIT_NX;
            __atomic_store_n(&current_table[index], entry, __ATOMIC_SEQ_CST);

            current_table = (uint64_t*) PTM_TO_HHDM(current_table[index] & SMALL_PAGE_ADDRESS_MASK);
        }

        int index = VADDR_TO_INDEX(vaddr + i, j);
        uint64_t entry = current_table[index] | privilege_to_x86_flags(privilege) | cache_to_x86_flags(cache, j == 0 ? ARCH_PAGE_SIZE_4K : (j == 1 ? ARCH_PAGE_SIZE_2M : ARCH_PAGE_SIZE_1G));

        if(prot.write)
            entry |= PAGE_BIT_RW;
        else
            entry &= ~PAGE_BIT_RW;

        if(!prot.execute)
            entry |= PAGE_BIT_NX;
        else
            entry &= ~PAGE_BIT_NX;

        if(global)
            entry |= PAGE_BIT_GLOBAL;
        else
            entry &= ~PAGE_BIT_GLOBAL;

        __atomic_store_n(&current_table[index], entry, __ATOMIC_SEQ_CST);

    skip:
        i += LEVEL_TO_PAGESIZE(j);
    }

    ptm_flush_tlb(vaddr, length);
    // @todo: ipi
    spinlock_nodw_unlock(&address_space->ptm.ptm_lock);
    return true;
}


void ptm_unmap(vm_address_space_t* address_space, uintptr_t vaddr, size_t length) {
    assert(vaddr % ARCH_PAGE_SIZE_4K == 0);
    assert(length % ARCH_PAGE_SIZE_4K == 0);

    spinlock_nodw_lock(&address_space->ptm.ptm_lock);

    for(size_t i = 0; i < length;) {
        uint64_t* path_tables[5];
        int path_indices[5];
        int path_depth = 0;

        uint64_t* current_table = (uint64_t*) PTM_TO_HHDM(address_space->ptm.top_level_page_table);

        int j = LEVEL_COUNT;
        for(; j > 1; j--) {
            int index = VADDR_TO_INDEX(vaddr + i, j);
            uint64_t entry = current_table[index];

            if((entry & PAGE_BIT_PRESENT) == 0) goto skip;
            if((entry & HUGE_PAGE_BIT_PAGE_STOP) != 0) {
                assert(j <= 3);
                if(INDEX_TO_VADDR(index, j) < vaddr + i || LEVEL_TO_PAGESIZE(j) > length - i) {
                    entry = break_large_page(current_table, index, j);
                    if(entry == 0) goto skip;
                } else {
                    break;
                }
            }

            path_tables[path_depth] = current_table;
            path_indices[path_depth] = index;
            path_depth++;

            current_table = (uint64_t*) PTM_TO_HHDM(entry & SMALL_PAGE_ADDRESS_MASK);
        }

        {
            int leaf_index = VADDR_TO_INDEX(vaddr + i, j);
            uint64_t prev = current_table[leaf_index];

            if(prev & PAGE_BIT_PRESENT) {
                __atomic_store_n(&current_table[leaf_index], 0, __ATOMIC_SEQ_CST);

                uint64_t* child_table = current_table;
                for(int d = path_depth - 1; d >= 0; d--) {
                    uint16_t old_count = ATOMIC_LOAD_SUB(&pagedb_get_page(table_to_pfn(child_table))->page_level_mapping_count, (uint16_t) 1, __ATOMIC_SEQ_CST);

                    if(old_count != 1) break;

                    uint64_t* parent_table = path_tables[d];
                    int parent_index = path_indices[d];
                    __atomic_store_n(&parent_table[parent_index], 0, __ATOMIC_SEQ_CST);
                    pmm_free_page((uintptr_t) PTM_FROM_HHDM(child_table));

                    child_table = parent_table;
                }
            }
        }

    skip:
        i += LEVEL_TO_PAGESIZE(j);
    }

    ptm_flush_tlb(vaddr, length);
    // @todo: ipi
    spinlock_nodw_unlock(&address_space->ptm.ptm_lock);
}

bool internal_ptm_physical(vm_address_space_t* address_space, uintptr_t vaddr, uintptr_t* paddr) {
    uint64_t* current_table = (uint64_t*) PTM_TO_HHDM(address_space->ptm.top_level_page_table);
    int j = LEVEL_COUNT;
    for(; j > 1; j--) {
        int index = VADDR_TO_INDEX(vaddr, j);
        if((current_table[index] & PAGE_BIT_PRESENT) == 0) { return false; }
        if((current_table[index] & HUGE_PAGE_BIT_PAGE_STOP) != 0) break;
        current_table = (uint64_t*) PTM_TO_HHDM(current_table[index] & SMALL_PAGE_ADDRESS_MASK);
    }

    uint64_t entry = current_table[VADDR_TO_INDEX(vaddr, j)];

    if((entry & PAGE_BIT_PRESENT) == 0) return false;

    switch(j) {
        case 1:  *paddr = ((entry & SMALL_PAGE_ADDRESS_MASK) + (vaddr & (~0x000F'FFFF'FFFF'F000))); break;
        case 2:  *paddr = ((entry & HUGE_PAGE_ADDRESS_MASK) + (vaddr & (~0x000F'FFFF'FFE0'0000))); break;
        case 3:  *paddr = ((entry & HUGE_PAGE_ADDRESS_MASK) + (vaddr & (~0x000F'FFFF'C000'0000))); break;
        default: __builtin_unreachable();
    }
    return true;
}

bool ptm_physical(vm_address_space_t* address_space, uintptr_t vaddr, uintptr_t* paddr) {
    spinlock_nodw_lock(&address_space->ptm.ptm_lock);
    bool result = internal_ptm_physical(address_space, vaddr, paddr);
    spinlock_nodw_unlock(&address_space->ptm.ptm_lock);
    return result;
}

void ptm_load_address_space(vm_address_space_t* address_space) {
    arch_cr_write_cr3(address_space->ptm.top_level_page_table);
}


void map_kernel() {
    for(size_t i = 0; i < g_init_boot_info->kernel_segment_count; i++) {
        bootinfo_segment_t* seg = &g_init_boot_info->kernel_segments[i];

        vm_protection_t prot = {
            .read = (seg->flags & BOOTINFO_SEGMENT_FLAG_READ) != 0,
            .write = (seg->flags & BOOTINFO_SEGMENT_FLAG_WRITE) != 0,
            .execute = (seg->flags & BOOTINFO_SEGMENT_FLAG_EXECUTE) != 0,
        };

        const phys_addr_t aligned_paddr = ALIGN_DOWN(seg->paddr, ARCH_PAGE_SIZE_4K);
        const virt_addr_t aligned_vaddr = ALIGN_DOWN(seg->vaddr, ARCH_PAGE_SIZE_4K);
        const size_t alignment_diff = seg->paddr - aligned_paddr;
        const size_t aligned_length = ALIGN_UP(seg->size + alignment_diff, ARCH_PAGE_SIZE_4K);

        ptm_map(g_vm_global_address_space, aligned_vaddr, aligned_paddr, aligned_length, prot, VM_CACHE_NORMAL, VM_PRIVILEGE_KERNEL, true);
    }

    for(size_t i = 0; i < g_init_boot_info->mm_entry_count; i++) {
        bootinfo_mm_entry_t* entry = &g_init_boot_info->mm_entries[i];

        if(entry->type == BOOTINFO_MM_TYPE_BAD) continue;

        const phys_addr_t aligned_paddr = ALIGN_DOWN(entry->phys_base, ARCH_PAGE_SIZE_4K);
        const virt_addr_t aligned_vaddr = (virt_addr_t) PTM_TO_HHDM(aligned_paddr);
        const size_t alignment_diff = entry->phys_base - aligned_paddr;
        const size_t aligned_length = ALIGN_UP(entry->length + alignment_diff, ARCH_PAGE_SIZE_4K);

        ptm_map(g_vm_global_address_space, aligned_vaddr, aligned_paddr, aligned_length, VM_PROT_RW, VM_CACHE_NORMAL, VM_PRIVILEGE_KERNEL, true);
    }

    extern char kernel_start[];
    extern char kernel_end[];

    virt_addr_t kernel_virt = (virt_addr_t) kernel_start;
    virt_addr_t kernel_size = ((virt_addr_t) kernel_end) - ((virt_addr_t) kernel_start);

    g_kernel_region.address_space = g_vm_global_address_space;
    g_kernel_region.base = kernel_virt;
    g_kernel_region.length = kernel_size;
    g_kernel_region.protection = VM_PROT_RX;
    g_kernel_region.cache = VM_CACHE_NORMAL;
    g_kernel_region.dynamically_backed = false;
    g_kernel_region.type = VM_REGION_TYPE_DIRECT;
    g_kernel_region.type_data.direct.physical_address = 0;

    g_hhdm_region.address_space = g_vm_global_address_space;
    g_hhdm_region.base = g_init_boot_info->hhdm_offset;
    g_hhdm_region.length = g_init_boot_info->hhdm_size;
    g_hhdm_region.protection = VM_PROT_RW;
    g_hhdm_region.cache = VM_CACHE_NORMAL;
    g_hhdm_region.dynamically_backed = false;
    g_hhdm_region.type = VM_REGION_TYPE_DIRECT;
    g_hhdm_region.type_data.direct.physical_address = 0;

    rb_insert(&g_vm_global_address_space->regions_tree, &g_kernel_region.region_tree_node);
    rb_insert(&g_vm_global_address_space->regions_tree, &g_hhdm_region.region_tree_node);
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
    g_la57_enabled = arch_cr_read_cr4() & (1 << 12); // cr4.LA57

    uint64_t* pml4 = (uint64_t*) PTM_TO_HHDM(g_vm_global_address_space->ptm.top_level_page_table);
    for(int i = 256; i < 512; i++) { pml4[i] = PAGE_BIT_PRESENT | PAGE_BIT_RW | (pmm_alloc_page(PMM_FLAG_ZERO | PMM_FLAG_PANIC) & SMALL_PAGE_ADDRESS_MASK); }
    map_kernel();

    ptm_load_address_space(g_vm_global_address_space);
    LOG_OKAY("switched to kernel page tables :33\n");

    // for(size_t i = 0; i < g_bootloader_info.framebuffer_count; i++) {
    //     bootloader_framebuffer_info_t framebuffer;
    //     bootloader_get_framebuffer(i, &framebuffer);
    //     const phys_addr_t paddr = framebuffer.paddr;

    //     const phys_addr_t aligned_paddr = ALIGN_DOWN(paddr, PAGE_SIZE_DEFAULT);
    //     const virt_addr_t alignment_diff = paddr - aligned_paddr;
    //     const size_t aligned_length = ALIGN_UP((framebuffer.framebuffer_length) + alignment_diff, PAGE_SIZE_DEFAULT);

    //     const void* new_vaddr = vm_map_direct(g_vm_global_address_space, VM_NO_HINT, aligned_length, VM_PROT_RW, VM_CACHE_WRITE_COMBINE, aligned_paddr, VM_FLAG_NONE);
    //     bootloader_set_framebuffer_address(i, (void*) new_vaddr + alignment_diff);
    // }

    // interrupt_set_handler(0x0E, page_fault_handler);
}

void ptm_init_kernel_ap() {}

bool ptm_init_user(vm_address_space_t* address_space) {
    address_space->ptm.top_level_page_table = pmm_alloc_page(PMM_FLAG_ZERO);
    if(address_space->ptm.top_level_page_table == 0) return false;

    address_space->ptm.ptm_lock = SPINLOCK_NO_DW_INIT;
    address_space->is_user = true;
    address_space->lock = SPINLOCK_NO_DW_INIT;
    address_space->regions_tree = vm_create_regions();
    address_space->start = MEMORY_USERSPACE_START;
    address_space->end = MEMORY_USERSPACE_END;

    uint64_t* user_pml4 = (uint64_t*) PTM_TO_HHDM(address_space->ptm.top_level_page_table);
    uint64_t* kernel_pml4 = (uint64_t*) PTM_TO_HHDM(g_vm_global_address_space->ptm.top_level_page_table);
    for(int i = 256; i < 512; i++) user_pml4[i] = kernel_pml4[i];

    return true;
}
