#include <arch/memory.h>
#include <common/assert.h>
#include <common/init.h>
#include <common/log.h>
#include <common/sync/spinlock.h>
#include <lib/string.h>
#include <memory/pagedb.h>
#include <memory/pmm.h>
#include <memory/ptm.h>
#include <protocol/bootinfo.h>

typedef struct pmm_entry {
    struct pmm_entry* next;
} pmm_entry_t;

struct pmm_entry* g_pmm_free_list;
spinlock_no_dw_t g_pmm_lock = SPINLOCK_NO_DW_INIT;

void pmm_init() {
    for(size_t i = 0; i < g_init_boot_info->mm_entry_count; i++) {
        bootinfo_mm_entry_t* entry = &g_init_boot_info->mm_entries[i];
        if(entry->type != BOOTINFO_MM_TYPE_USABLE) { continue; }

        uintptr_t start = entry->phys_base;
        uintptr_t end = entry->phys_base + entry->length;
        for(uintptr_t addr = start; addr < end; addr += ARCH_PAGE_SIZE_DEFAULT) {
            pagedb_page_t* page = pagedb_get_page(addr >> 12);
            assert(page != nullptr);
            ATOMIC_STORE(&page->ref_count, 0, ATOMIC_RELAXED);
            ATOMIC_STORE(&page->map_count, 0, ATOMIC_RELAXED);
            ATOMIC_STORE(&page->page_level_mapping_count, 0, ATOMIC_RELAXED);

            pmm_entry_t* new_entry = (pmm_entry_t*) PTM_TO_HHDM(addr);
            new_entry->next = g_pmm_free_list;
            g_pmm_free_list = new_entry;
        }
    }
}

uintptr_t pmm_alloc_page(pmm_flag_t flags) {
    spinlock_nodw_lock(&g_pmm_lock);
    if(g_pmm_free_list == NULL) {
        spinlock_nodw_unlock(&g_pmm_lock);
        if(flags & PMM_FLAG_PANIC) { arch_panic("out of physical memory!"); }
        return 0;
    }
    pmm_entry_t* allocated_entry = g_pmm_free_list;
    g_pmm_free_list = g_pmm_free_list->next;
    spinlock_nodw_unlock(&g_pmm_lock);

    uintptr_t addr = (uintptr_t) PTM_FROM_HHDM(allocated_entry);
    pagedb_page_ref(addr >> 12);

    if(flags & PMM_FLAG_ZERO) { memset((void*) PTM_TO_HHDM(addr), 0, ARCH_PAGE_SIZE_DEFAULT); }
    return addr;
}

void pmm_free_page(uintptr_t addr) {
    pmm_entry_t* new_entry = (pmm_entry_t*) PTM_TO_HHDM(addr);

    if(pagedb_page_deref(addr >> 12) != 0) {
        LOG_OKAY("not freeing page 0x%016lx, ref count is not 0\n", addr);
        return;
    }

    spinlock_nodw_lock(&g_pmm_lock);
    new_entry->next = g_pmm_free_list;
    g_pmm_free_list = new_entry;
    spinlock_nodw_unlock(&g_pmm_lock);
}
