#pragma once
#include <lib/helpers.h>
#include <stdint.h>

typedef enum {
    PAGEDB_ENTRY_TYPE_RESERVED = 0,
    PAGEDB_ENTRY_TYPE_FREE = 1,
    PAGEDB_ENTRY_TYPE_ALLOCATED = 2
} pagedb_entry_type_t;

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

    ATOMIC pagedb_entry_type_t type;
} pagedb_page_t;

void pagedb_init();

/// @brief Returns true if the pfn is vaild physical page
bool pagedb_valid_page(uint64_t pfn);

/// @brief Returns the backing pagedb entry of the target pfn
pagedb_page_t* pagedb_get_page(uint64_t pfn);

/// @brief Increment the reference count of a pfn
/// @returns the current refrence count
uint32_t pagedb_page_ref(uint64_t pfn);

/// @brief Decrement the reference count of a pfn
/// @returns the current refrence count
/// @note this does not free the page
uint32_t pagedb_page_deref(uint64_t pfn);

/// @brief Increment the map count of a pfn
/// @returns the current map count
/// @note this does not free the page
uint32_t pagedb_page_map(uint64_t pfn);

/// @brief Decrement the map count of a pfn
/// @returns the current map count
/// @note this does not free the page
uint32_t pagedb_page_unmap(uint64_t pfn);

/// @brief Set the entry type of a pfn
void pagedb_set_page_type(uint64_t pfn, pagedb_entry_type_t entry_type);
