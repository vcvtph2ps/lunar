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
        LOG_STRC("pagedb_get_page: pfn=0x%lx out of range (count=0x%lx)\n", pfn, count);
        return nullptr;
    }
    if(!pagedb_valid_page(pfn)) { return nullptr; }
    return &g_pagedb_start[pfn];
}

uint32_t pagedb_page_ref(uint64_t pfn) {
    pagedb_page_t* page = pagedb_get_page(pfn);
    assert(page != nullptr);
    return ATOMIC_LOAD_ADD(&page->ref_count, 1, ATOMIC_ACQUIRE) + 1;
}

uint32_t pagedb_page_deref(uint64_t pfn) {
    pagedb_page_t* page = pagedb_get_page(pfn);
    assert(page != nullptr);
    return ATOMIC_LOAD_SUB(&page->ref_count, 1, ATOMIC_RELEASE) - 1;
}

uint32_t pagedb_page_map(uint64_t pfn) {
    pagedb_page_t* page = pagedb_get_page(pfn);
    assert(page != nullptr);
    return ATOMIC_LOAD_ADD(&page->map_count, 1, ATOMIC_ACQUIRE) + 1;
}

uint32_t pagedb_page_unmap(uint64_t pfn) {
    pagedb_page_t* page = pagedb_get_page(pfn);
    assert(page != nullptr);
    return ATOMIC_LOAD_SUB(&page->map_count, 1, ATOMIC_RELEASE) - 1;
}
