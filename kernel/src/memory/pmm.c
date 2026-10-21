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

#define PMM_LOCAL_PAGE_MIN 25
#define PMM_LOCAL_PAGE_MAX 30

static pmm_t g_pmm;

static void populate_global() {
    g_pmm.num_pages = 0;
    g_pmm.free_list_pfn = 0;
    g_pmm.pmm_lock = SPINLOCK_NO_DW_INIT;

    for(size_t i = 0; i < g_init_boot_info->mm_entry_count; i++) {
        bootinfo_mm_entry_t* entry = &g_init_boot_info->mm_entries[i];
        if(entry->type != BOOTINFO_MM_TYPE_USABLE) {
            continue;
        }

        uintptr_t start = entry->phys_base;
        uintptr_t end = entry->phys_base + entry->length;
        for(uintptr_t addr = start; addr < end; addr += ARCH_PAGE_SIZE_DEFAULT) {
            uint64_t pfn = addr >> 12;
            pagedb_page_t* page = pagedb_get_page(pfn);
            assert(page != nullptr);
            ATOMIC_STORE(&page->type, PAGEDB_ENTRY_TYPE_FREE, ATOMIC_RELAXED);
            ATOMIC_STORE(&page->free.next_pfn, g_pmm.free_list_pfn, ATOMIC_RELAXED);
            g_pmm.free_list_pfn = pfn;
            g_pmm.num_pages++;
        }
    }
}

static uintptr_t alloc_page_from(pmm_t* pmm, pmm_flag_t flags) {
    spinlock_nodw_lock(&pmm->pmm_lock);
    if(pmm->num_pages == 0) {
        spinlock_nodw_unlock(&pmm->pmm_lock);
        if(flags & PMM_FLAG_PANIC) {
            arch_panic("pmm: out of physical memory!");
        }
        LOG_WARN("pmm: out of physical memory!!\n");
        return 0;
    }

    uint64_t pfn = pmm->free_list_pfn;
    pagedb_page_t* page = pagedb_get_page(pfn);
    assert(page != nullptr);

    pmm->free_list_pfn = ATOMIC_LOAD(&page->free.next_pfn, ATOMIC_ACQUIRE);
    pmm->num_pages--;

    spinlock_nodw_unlock(&pmm->pmm_lock);

    uintptr_t addr = (uintptr_t) pfn << 12;
    pagedb_set_page_state(pfn, PAGEDB_ENTRY_TYPE_ALLOCATED);
    pagedb_page_ref(pfn);

#if defined(__RELEASE__)
    if(flags & PMM_FLAG_ZERO) {
        memory_zero((void*) PTM_TO_HHDM(addr), ARCH_PAGE_SIZE_DEFAULT);
    }
#else
    if(flags & PMM_FLAG_ZERO) {
        memory_zero((void*) PTM_TO_HHDM(addr), ARCH_PAGE_SIZE_DEFAULT);
    } else {
        memory_set((void*) PTM_TO_HHDM(addr), 0xcc, ARCH_PAGE_SIZE_DEFAULT);
    }
#endif

    return addr;
}

static void free_page_to(pmm_t* pmm, uintptr_t addr) {
    uint64_t pfn = addr >> 12;

    if(pagedb_page_deref(pfn) != 0) {
        LOG_OKAY("not freeing page 0x%016lx, ref count is not 0\n", addr);
        return;
    }

    pagedb_page_t* page = pagedb_get_page(pfn);
    assert(page != nullptr);
    pagedb_set_page_state(pfn, PAGEDB_ENTRY_TYPE_FREE);

    spinlock_nodw_lock(&pmm->pmm_lock);
    ATOMIC_STORE(&page->free.next_pfn, pmm->free_list_pfn, ATOMIC_RELEASE);
    pmm->free_list_pfn = pfn;
    pmm->num_pages++;
    spinlock_nodw_unlock(&pmm->pmm_lock);
}

static void transfer_pages_from(pmm_t* src, pmm_t* dest, size_t num_pages) {
    for(size_t i = 0; i < num_pages; i++) {
        if(src->num_pages == 0) {
            break;
        }

        uint64_t entry = alloc_page_from(src, PMM_FLAG_PANIC);
        free_page_to(dest, entry);
    }
}

void pmm_init(uint32_t core_id) {
    if(INIT_CORE_IS_BSP(core_id)) {
        populate_global();
    }

    CPU_LOCAL_WRITE(pmm.num_pages, 0ul);
    CPU_LOCAL_WRITE(pmm.free_list_pfn, 0ul);
    CPU_LOCAL_GET_SELF()->pmm.pmm_lock = SPINLOCK_NO_DW_INIT;
}


uintptr_t pmm_alloc_page(pmm_flag_t flags) {
    if(CPU_LOCAL_READ(pmm.num_pages) > 0) {
        return alloc_page_from(&CPU_LOCAL_GET_SELF()->pmm, flags);
    }

    transfer_pages_from(&g_pmm, &CPU_LOCAL_GET_SELF()->pmm, PMM_LOCAL_PAGE_MIN);

    uintptr_t addr = alloc_page_from(&CPU_LOCAL_GET_SELF()->pmm, flags);
    return addr;
}

void pmm_free_page(uintptr_t addr) {
    if(CPU_LOCAL_READ(pmm.num_pages) < PMM_LOCAL_PAGE_MAX) {
        free_page_to(&CPU_LOCAL_GET_SELF()->pmm, addr);
        transfer_pages_from(&CPU_LOCAL_GET_SELF()->pmm, &g_pmm, PMM_LOCAL_PAGE_MAX - PMM_LOCAL_PAGE_MIN);
    } else {
        free_page_to(&g_pmm, addr);
    }
}

void pmm_reclaim(uintptr_t addr, size_t page_count) {
    for(size_t i = 0; i < page_count; i++) {
        uintptr_t address = addr + (i * ARCH_PAGE_SIZE_DEFAULT);
        uint64_t pfn = address >> 12;

        pagedb_page_t* page = pagedb_get_page(pfn);
        assert(page != nullptr);
        pagedb_set_page_state(pfn, PAGEDB_ENTRY_TYPE_FREE);

        spinlock_nodw_lock(&g_pmm.pmm_lock);
        ATOMIC_STORE(&page->free.next_pfn, g_pmm.free_list_pfn, ATOMIC_RELEASE);
        g_pmm.free_list_pfn = pfn;
        g_pmm.num_pages++;
        spinlock_nodw_unlock(&g_pmm.pmm_lock);
    }
}
