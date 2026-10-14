#pragma once
#include <lib/helpers.h>
#include <stdint.h>

#define PAGEDB_ENTRY_TYPE_RESERVED 0
#define PAGEDB_ENTRY_TYPE_FREE 1
#define PAGEDB_ENTRY_TYPE_ALLOCATED 2

typedef struct [[gnu::aligned(16)]] {
    union {
        struct {
            ATOMIC uint64_t next_pfn;
        } free;
        struct {
            ATOMIC uint32_t ref_count;
            ATOMIC uint32_t map_count;
            ATOMIC uint16_t page_level_mapping_count;
            uint16_t pad;
        } allocated;
    };

    ATOMIC uint8_t type;
} pagedb_page_t;

void pagedb_init();

bool pagedb_valid_page(uint64_t pfn);
pagedb_page_t* pagedb_get_page(uint64_t pfn);
uint32_t pagedb_page_ref(uint64_t pfn);
uint32_t pagedb_page_deref(uint64_t pfn);
uint32_t pagedb_page_map(uint64_t pfn);
uint32_t pagedb_page_unmap(uint64_t pfn);
void pagedb_set_page_state(uint64_t pfn, uint8_t entry_type);
