#include <common/assert.h>
#include <common/init.h>
#include <common/log.h>
#include <common/memory.h>
#include <memory/pagedb.h>

pagedb_page_t* g_pagedb_start;

void pagedb_init() {
    g_pagedb_start = (pagedb_page_t*) g_init_boot_info->pfndb_start;
}

bool pagedb_valid_page(uint64_t pfn) {
    if(pfn >= g_init_boot_info->pfndb_size / sizeof(pagedb_page_t)) { return false; }

    const uint64_t page_index = pfn * sizeof(pagedb_page_t) / ARCH_PAGE_SIZE_4K;
    if(page_index / 8 >= g_init_boot_info->pfndb_bitmap_size) { return false; }

    const uint8_t* bitmap = (const uint8_t*) g_init_boot_info->pfndb_bitmap_start;
    return (bitmap[page_index / 8] & (1u << (page_index % 8))) != 0;
}

pagedb_page_t* pagedb_get_page(uint64_t pfn) {
    uint64_t count = g_init_boot_info->pfndb_size / sizeof(pagedb_page_t);
    if(pfn >= count) {
        LOG_KTRC("pagedb_get_page: pfn=0x%lx out of range (count=0x%lx)\n", pfn, count);
        return nullptr;
    }
    if(!pagedb_valid_page(pfn)) { return nullptr; }
    return &g_pagedb_start[pfn];
}

uint32_t pagedb_page_ref(uint64_t pfn) {
    pagedb_page_t* page = pagedb_get_page(pfn);
    assert(page != nullptr);
    assert(ATOMIC_LOAD(&page->type, ATOMIC_ACQUIRE) == PAGEDB_ENTRY_TYPE_ALLOCATED);
    return ATOMIC_LOAD_ADD(&page->allocated.ref_count, 1, ATOMIC_ACQUIRE) + 1;
}

uint32_t pagedb_page_deref(uint64_t pfn) {
    pagedb_page_t* page = pagedb_get_page(pfn);
    assert(page != nullptr);
    assert(ATOMIC_LOAD(&page->type, ATOMIC_ACQUIRE) == PAGEDB_ENTRY_TYPE_ALLOCATED);
    return ATOMIC_LOAD_SUB(&page->allocated.ref_count, 1, ATOMIC_RELEASE) - 1;
}

uint32_t pagedb_page_map(uint64_t pfn) {
    pagedb_page_t* page = pagedb_get_page(pfn);
    assert(page != nullptr);
    assert(ATOMIC_LOAD(&page->type, ATOMIC_ACQUIRE) == PAGEDB_ENTRY_TYPE_ALLOCATED);
    return ATOMIC_LOAD_ADD(&page->allocated.map_count, 1, ATOMIC_ACQUIRE) + 1;
}

uint32_t pagedb_page_unmap(uint64_t pfn) {
    pagedb_page_t* page = pagedb_get_page(pfn);
    assert(page != nullptr);
    assert(ATOMIC_LOAD(&page->type, ATOMIC_ACQUIRE) == PAGEDB_ENTRY_TYPE_ALLOCATED);
    return ATOMIC_LOAD_SUB(&page->allocated.map_count, 1, ATOMIC_RELEASE) - 1;
}

void pagedb_set_page_state(uint64_t pfn, uint8_t entry_type) {
    pagedb_page_t* page = pagedb_get_page(pfn);
    if(page == nullptr) {
        arch_panic("called pagedb_set_page_state on invalid page");
    }

    ATOMIC_STORE(&page->type, entry_type, ATOMIC_RELAXED);
    if(entry_type == PAGEDB_ENTRY_TYPE_FREE) {
        ATOMIC_STORE(&page->free.next_pfn, 0, ATOMIC_RELAXED);
    } else if(entry_type == PAGEDB_ENTRY_TYPE_ALLOCATED) {
        ATOMIC_STORE(&page->allocated.ref_count, 0, ATOMIC_RELAXED);
        ATOMIC_STORE(&page->allocated.map_count, 0, ATOMIC_RELAXED);
        ATOMIC_STORE(&page->allocated.page_level_mapping_count, 0, ATOMIC_RELAXED);
    } else {
        arch_panic("called pagedb_set_page with invaild state");
    }
}
