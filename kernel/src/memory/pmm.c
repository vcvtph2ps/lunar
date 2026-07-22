#include <common/assert.h>
#include <common/cpu_local.h>
#include <common/init.h>
#include <common/log.h>
#include <common/memory.h>
#include <common/sync/spinlock.h>
#include <lib/string.h>
#include <memory/pagedb.h>
#include <memory/pmm.h>
#include <memory/ptm.h>
#include <protocol/bootinfo.h>

#define PMM_LOCAL_PAGE_LIMIT 25

static pmm_t g_pmm;

static void populate_global() {
    g_pmm.num_pages = 0;
    g_pmm.free_list = nullptr;
    g_pmm.pmm_lock = SPINLOCK_NO_DW_INIT;

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
            new_entry->next = g_pmm.free_list;
            g_pmm.free_list = new_entry;
            g_pmm.num_pages++;
        }
    }
}

static uintptr_t alloc_page_from(pmm_t* pmm, pmm_flag_t flags) {
    spinlock_nodw_lock(&pmm->pmm_lock);
    if(pmm->num_pages == 0) {
        spinlock_nodw_unlock(&pmm->pmm_lock);
        if(flags & PMM_FLAG_PANIC) { arch_panic("out of physical memory!"); }
        return 0;
    }
    pmm_entry_t* allocated_entry = pmm->free_list;
    pmm->free_list = pmm->free_list->next;
    pmm->num_pages--;
    spinlock_nodw_unlock(&pmm->pmm_lock);

    uintptr_t addr = (uintptr_t) PTM_FROM_HHDM(allocated_entry);
    pagedb_page_ref(addr >> 12);

#if defined(__RELEASE__)
    if(flags & PMM_FLAG_ZERO) { memset((void*) PTM_TO_HHDM(addr), 0, ARCH_PAGE_SIZE_DEFAULT); }
#else
    if(flags & PMM_FLAG_ZERO) {
        memset((void*) PTM_TO_HHDM(addr), 0, ARCH_PAGE_SIZE_DEFAULT);
    } else {
        memset((void*) PTM_TO_HHDM(addr), 0xcc, ARCH_PAGE_SIZE_DEFAULT);
    }
#endif


    return addr;
}

static void free_page_to(pmm_t* pmm, uintptr_t addr) {
    pmm_entry_t* new_entry = (pmm_entry_t*) PTM_TO_HHDM(addr);

    if(pagedb_page_deref(addr >> 12) != 0) {
        LOG_OKAY("not freeing page 0x%016lx, ref count is not 0\n", addr);
        return;
    }

    spinlock_nodw_lock(&pmm->pmm_lock);
    new_entry->next = pmm->free_list;
    pmm->free_list = new_entry;
    spinlock_nodw_unlock(&pmm->pmm_lock);
}

static void transfer_pages_from(pmm_t* src, pmm_t* dest, size_t num_pages) {
    spinlock_nodw_lock(&src->pmm_lock);
    spinlock_nodw_lock(&dest->pmm_lock);

    for(size_t i = 0; i < num_pages; i++) {
        if(src->num_pages == 0) { break; }
        pmm_entry_t* entry = src->free_list;
        src->free_list = entry->next;
        src->num_pages--;

        entry->next = dest->free_list;
        dest->free_list = entry;
        dest->num_pages++;
    }

    spinlock_nodw_unlock(&dest->pmm_lock);
    spinlock_nodw_unlock(&src->pmm_lock);
}

void pmm_init(uint32_t core_id) {
    if(INIT_CORE_IS_BSP(core_id)) { populate_global(); }
    CPU_LOCAL_GET_SELF()->pmm.num_pages = 0ul;
    CPU_LOCAL_GET_SELF()->pmm.free_list = (pmm_entry_t*) nullptr;
    CPU_LOCAL_GET_SELF()->pmm.pmm_lock = SPINLOCK_NO_DW_INIT;
}


uintptr_t pmm_alloc_page(pmm_flag_t flags) {
    if(CPU_LOCAL_READ(pmm.num_pages) > 0) { return alloc_page_from(&CPU_LOCAL_GET_SELF()->pmm, flags); }

    transfer_pages_from(&g_pmm, &CPU_LOCAL_GET_SELF()->pmm, PMM_LOCAL_PAGE_LIMIT);

    uintptr_t addr = alloc_page_from(&CPU_LOCAL_GET_SELF()->pmm, flags);
    return addr;
}

void pmm_free_page(uintptr_t addr) {
    if(CPU_LOCAL_READ(pmm.num_pages) < PMM_LOCAL_PAGE_LIMIT) {
        free_page_to(&CPU_LOCAL_GET_SELF()->pmm, addr);
    } else {
        free_page_to(&g_pmm, addr);
    }
}
