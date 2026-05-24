#include <arch/memory.h>
#include <common/init.h>
#include <common/log.h>
#include <common/spinlock.h>
#include <memory/pagedb.h>
#include <memory/pmm.h>
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
        for(uintptr_t addr = start; addr < end; addr += ARCH_MEMORY_PAGE_SIZE) {
            pmm_entry_t* new_entry = (pmm_entry_t*) (addr + g_init_boot_info->hhdm_offset);
            new_entry->next = g_pmm_free_list;
            g_pmm_free_list = new_entry;
        }
    }
}

uintptr_t pmm_alloc_page() {
    spinlock_nodw_lock(&g_pmm_lock);
    if(g_pmm_free_list == NULL) {
        spinlock_nodw_unlock(&g_pmm_lock);
        return 0;
    }
    pmm_entry_t* allocated_entry = g_pmm_free_list;
    g_pmm_free_list = g_pmm_free_list->next;
    spinlock_nodw_unlock(&g_pmm_lock);

    uintptr_t addr = (uintptr_t) allocated_entry - g_init_boot_info->hhdm_offset;
    pagedb_page_ref(addr >> 12);
    return addr;
}

void pmm_free_page(uintptr_t addr) {
    pmm_entry_t* new_entry = (pmm_entry_t*) (addr + g_init_boot_info->hhdm_offset);

    if(pagedb_page_deref(addr >> 12) != 0) {
        LOG_OKAY("not freeing page 0x%016lx, ref count is not 0\n", addr);
        return;
    }
    spinlock_nodw_lock(&g_pmm_lock);
    new_entry->next = g_pmm_free_list;
    g_pmm_free_list = new_entry;
    spinlock_nodw_unlock(&g_pmm_lock);
}
