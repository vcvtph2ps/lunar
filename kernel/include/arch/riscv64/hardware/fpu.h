#pragma once
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Saves the current fpu state into the buffer at ptr
 * @param ptr the buffer to store the state
 */
void arch_fpu_save(void* ptr);

/**
 * @brief Loads the fpu state from the buffer at ptr
 * @param ptr the buffer to load the state from
 */
void arch_fpu_load(void* ptr);

/**
 * @brief Initializes the FPU for the current core.
 * @param core_id The ID of the current core.
 */
void arch_fpu_init(uint32_t core_id);

/**
 * @brief Allocates a buffer for the correct size to be used with save/load
 * @returns Allocated FPU buffer
 */
void* arch_fpu_alloc_area();

/**
 * @brief Frees the buffer returned by arch_fpu_alloc_area
 * @param area Target buffer to free
 */
void arch_fpu_free_area(void* area);

/**
 * @brief Gets the size of an FPU save area
 * @returns FPU save area size in bytes
 */
size_t arch_fpu_area_size();
