#pragma once
#include <lib/helpers.h>
#include <stdint.h>

typedef struct [[gnu::packed, gnu::aligned(16)]] {
    ATOMIC uint32_t ref_count;
    ATOMIC uint32_t map_count;
    ATOMIC uint32_t page_level_mapping_count;
    uint32_t padding;
} pagedb_page_t;

void pagedb_init();

pagedb_page_t* pagedb_get_page(uint64_t pfn);
uint32_t pagedb_page_ref(uint64_t pfn);
uint32_t pagedb_page_deref(uint64_t pfn);
uint32_t pagedb_page_map(uint64_t pfn);
uint32_t pagedb_page_unmap(uint64_t pfn);
