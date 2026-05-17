#include <arch/cpuid.h>
#include <arch/cr.h>
#include <arch/msr.h>
#include <common/mem.h>
#include <lib/math.h>
#include <log.h>
#include <memory/pmm.h>
#include <memory/ptm.h>
#include <panic.h>
#include <protocol/bootinfo.h>

extern bootinfo_t* g_pk_boot_info;

#define ENTRY_FLAG_PRESENT (1 << 0)
#define ENTRY_FLAG_RW (1 << 1)
#define ENTRY_FLAG_NX ((uint64_t) 1 << 63)

#define ENTRYH_FLAG_PS (1 << 7)

#define ENTRY_4K_ADDRESS_MASK ((uint64_t) 0x0007'FFFF'FFFF'F000)
#define ENTRY_2M_ADDRESS_MASK ((uint64_t) 0x0007'FFFF'FFE0'0000)
#define ENTRY_1G_ADDRESS_MASK ((uint64_t) 0x0007'FFFF'C000'0000)

#define VADDR_TO_INDEX(VADDR, LEVEL) (((VADDR) >> ((LEVEL) * 9 + 3)) & 0x1FF)

ptm_t g_ptm = {};
static bool g_x86_64_cpu_nx_support = false;
static bool g_x86_64_cpu_pdpe1gb_support = false;

void pk_ptm_init() {
    uintptr_t top_pagemap = (uintptr_t) pk_pmm_alloc(1);
    memset((void*) (top_pagemap + g_pk_boot_info->hhdm_offset), 0, PTM_PAGE_GRANULARITY);
    if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_LA57)) {
        if(arch_cr_read_cr4() & (1 << 12)) {
            g_ptm.level_count = 5;
        } else {
            pk_log_print("LA57 supported but not enabled, falling back to 4-level paging\n");
            g_ptm.level_count = 4;
        }
    } else {
        g_ptm.level_count = 4;
    }

    g_ptm.tplt = top_pagemap;

    g_x86_64_cpu_nx_support = arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_NX_PAGES);
    g_x86_64_cpu_pdpe1gb_support = arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_PDPE1GB_PAGES);

    pk_log_print("ptm init: level=%zu, nx %s, pdpe1gb %s\n", g_ptm.level_count, g_x86_64_cpu_nx_support ? "available" : "not available", g_x86_64_cpu_pdpe1gb_support ? "available" : "not available");
}


static void map_page(uint64_t vaddr, uint64_t paddr, ptm_page_size_t page_size, bool rw, bool nx) {
    int lowest_level;
    switch(page_size) {
        case PTM_PAGE_SIZE_4K: lowest_level = 1; break;
        case PTM_PAGE_SIZE_2M: lowest_level = 2; break;
        case PTM_PAGE_SIZE_1G: lowest_level = 3; break;
    }

    uint64_t* current_table = (uint64_t*) (g_ptm.tplt + g_pk_boot_info->hhdm_offset);
    for(int level = g_ptm.level_count; level > lowest_level; level--) {
        int index = VADDR_TO_INDEX(vaddr, level);

        uint64_t entry = current_table[index];
        if((entry & ENTRY_FLAG_PRESENT) == 0) {
            uint64_t* new_table = pk_pmm_alloc(1);
            memset((void*) (((uintptr_t) new_table) + g_pk_boot_info->hhdm_offset), 0, PTM_PAGE_GRANULARITY);
            entry = ENTRY_FLAG_PRESENT | ((uint64_t) (uintptr_t) new_table & ENTRY_4K_ADDRESS_MASK);
            if(nx) entry |= ENTRY_FLAG_NX;
        } else {
            if((entry & ENTRYH_FLAG_PS) != 0) pk_panic("cannot remap over a non-4k page %#llx", entry & ENTRY_4K_ADDRESS_MASK);
            if(!nx) entry &= ~ENTRY_FLAG_NX;
        }
        if(rw) entry |= ENTRY_FLAG_RW;

        if(current_table[index] != entry) current_table[index] = entry;

        current_table = (uint64_t*) ((entry & ENTRY_4K_ADDRESS_MASK) + g_pk_boot_info->hhdm_offset);
    }

    int index = VADDR_TO_INDEX(vaddr, lowest_level);

    uint64_t address_mask;
    switch(page_size) {
        case PTM_PAGE_SIZE_4K: address_mask = ENTRY_4K_ADDRESS_MASK; break;
        case PTM_PAGE_SIZE_2M: address_mask = ENTRY_2M_ADDRESS_MASK; break;
        case PTM_PAGE_SIZE_1G: address_mask = ENTRY_1G_ADDRESS_MASK; break;
    }

    uint64_t entry = ENTRY_FLAG_PRESENT | (paddr & address_mask);
    if(page_size != PTM_PAGE_SIZE_4K) entry |= ENTRYH_FLAG_PS;
    if(rw) entry |= ENTRY_FLAG_RW;
    if(nx) entry |= ENTRY_FLAG_NX;
    current_table[index] = entry;
}

void pk_ptm_map(uint64_t vaddr, uint64_t paddr, uint64_t length, uint8_t flags) {
    if(paddr % PTM_PAGE_GRANULARITY != 0 || vaddr % PTM_PAGE_GRANULARITY != 0 || length % PTM_PAGE_GRANULARITY != 0) pk_panic("unaligned mapping (%#llx -> %#llx / %#llx)", paddr, vaddr, length);
    if((flags & PTM_FLAG_READ) == 0) pk_log_print("mapping with no read permission\n");

    uint64_t offset = 0;
    while(offset < length) {
        ptm_page_size_t page_size = PTM_PAGE_SIZE_4K;
        if(paddr % PTM_PAGE_SIZE_2M == 0 && vaddr % PTM_PAGE_SIZE_2M == 0 && length - offset >= PTM_PAGE_SIZE_2M) page_size = PTM_PAGE_SIZE_2M;
        if(g_x86_64_cpu_pdpe1gb_support && paddr % PTM_PAGE_SIZE_1G == 0 && vaddr % PTM_PAGE_SIZE_1G == 0 && length - offset >= PTM_PAGE_SIZE_1G) page_size = PTM_PAGE_SIZE_1G;

        map_page(vaddr, paddr, page_size, (flags & PTM_FLAG_WRITE) != 0, g_x86_64_cpu_nx_support && (flags & PTM_FLAG_EXEC) == 0);
        paddr += page_size;
        vaddr += page_size;
        offset += page_size;
    }
}

void pk_ptm_create_hhdm_mappings() {
    size_t frozen_map_size = g_pmm_map_size;
    pmm_map_entry_t* frozen_map = pk_pmm_alloc(MATH_ALIGN_UP(sizeof(pmm_map_entry_t) * frozen_map_size, PTM_PAGE_GRANULARITY) / PTM_PAGE_GRANULARITY);
    memcpy(frozen_map, &g_pmm_map, sizeof(pmm_map_entry_t) * frozen_map_size);

    for(size_t i = 0; i < g_pmm_map_size; i++) {
        switch(frozen_map[i].type) {
            case PMM_MAP_TYPE_FREE:
            case PMM_MAP_TYPE_ALLOCATED:
            case PMM_MAP_TYPE_BOOTLOADER_RECLAIMABLE:
            case PMM_MAP_TYPE_EFI_RECLAIMABLE:
            case PMM_MAP_TYPE_ACPI_RECLAIMABLE:       break;
            default:                                  continue;
        }

        uint64_t base = frozen_map[i].base;
        uint64_t length = frozen_map[i].length;
        if(base % PTM_PAGE_GRANULARITY != 0) {
            length += base % PTM_PAGE_GRANULARITY;
            base -= base % PTM_PAGE_GRANULARITY;
        }
        if(length % PTM_PAGE_GRANULARITY != 0) length += PTM_PAGE_GRANULARITY - length % PTM_PAGE_GRANULARITY;

        pk_ptm_map(g_pk_boot_info->hhdm_offset + base, base, length, PTM_FLAG_READ | PTM_FLAG_WRITE);
    }
}
