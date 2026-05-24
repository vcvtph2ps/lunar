#pragma once
#include <stdint.h>

/**
 *
 */
void pmm_init();

/**
 *
 */
uintptr_t pmm_alloc_page();

/**
 *
 */
void pmm_free_page(uintptr_t addr);
