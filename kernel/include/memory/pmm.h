#pragma once
#include <stdint.h>

#define PMM_FLAG_NONE (0)
#define PMM_FLAG_ZERO (1 << 0)
#define PMM_FLAG_PANIC (1 << 1)

typedef uint8_t pmm_flag_t;

/**
 *
 */
void pmm_init();

/**
 *
 */
[[nodiscard]] uintptr_t pmm_alloc_page(pmm_flag_t flags);

/**
 *
 */
void pmm_free_page(uintptr_t addr);
