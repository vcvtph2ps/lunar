#include <common/assert.h>
#include <common/init.h>
#include <memory/pagedb.h>

pagedb_page_t* g_pagedb_start;

void pagedb_init() {
    g_pagedb_start = (pagedb_page_t*) g_init_boot_info->pfndb_start;
}

pagedb_page_t* pagedb_get_page(uint64_t pfn) {
    if(pfn >= g_init_boot_info->pfndb_size / sizeof(pagedb_page_t)) return nullptr;
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
