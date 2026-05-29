#pragma once
#include <lib/helpers.h>
#include <stdint.h>

typedef struct [[gnu::packed, gnu::aligned(16)]] {
    ATOMIC uint64_t ref_count;
    ATOMIC uint16_t page_level_mapping_count;
} pagedb_page_t;

void pagedb_init();

pagedb_page_t* pagedb_get_page(uint64_t pfn);
uint64_t pagedb_page_ref(uint64_t pfn);
uint64_t pagedb_page_deref(uint64_t pfn);
