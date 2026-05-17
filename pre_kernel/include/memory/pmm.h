#pragma once
#include <stddef.h>
#include <stdint.h>

// Minorly inspired from https://github.com/elysium-os/tartarus-bootloader
typedef enum {
    PMM_MAP_TYPE_FREE,
    PMM_MAP_TYPE_ALLOCATED,

    PMM_MAP_TYPE_USED,

    PMM_MAP_TYPE_BOOTLOADER_RECLAIMABLE,
    PMM_MAP_TYPE_EFI_RECLAIMABLE,
    PMM_MAP_TYPE_ACPI_RECLAIMABLE,
    PMM_MAP_TYPE_ACPI_NVS,
    PMM_MAP_TYPE_RESERVED,
    PMM_MAP_TYPE_BAD,
} pmm_map_type_t;

typedef struct {
    uint64_t base;
    uint64_t length;
    pmm_map_type_t type;
} pmm_map_entry_t;

#define PMM_MAP_MAX_ENTRIES 1024

extern size_t g_pmm_map_size;
extern pmm_map_entry_t g_pmm_map[PMM_MAP_MAX_ENTRIES];

void pk_pmm_map_add(uint64_t base, uint64_t length, pmm_map_type_t type); // NOLINT @todo: fix elysium tidy naming
void pk_pmm_map_set(uint64_t base, uint64_t length, pmm_map_type_t type, bool force); // NOLINT @todo: fix elysium tidy naming

[[nodiscard]] bool pk_pmm_alloc_at(uint64_t address, size_t page_count, pmm_map_type_t type); // NOLINT @todo: fix elysium tidy naming
[[nodiscard]] void* pk_pmm_alloc_ext(size_t page_count, size_t alignment, pmm_map_type_t type); // NOLINT @todo: fix elysium tidy naming
[[nodiscard]] void* pk_pmm_alloc(size_t page_count); // NOLINT @todo: fix elysium tidy naming
void pk_pmm_free(void* address, size_t count); // NOLINT @todo: fix elysium tidy naming
