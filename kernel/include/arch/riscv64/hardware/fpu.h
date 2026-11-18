#pragma once
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Saves the current fpu state into the buffer at ptr
 */
void arch_fpu_save(void* ptr);

/**
 * @brief Loads the fpu state from the buffer at ptr
 */
void arch_fpu_load(void* ptr);

/**
 * @brief Initializes the FPU for the current core
 */
void arch_fpu_init(uint32_t core_id);

/**
 * @brief Allocates a buffer for the correct size to be used with save/load
 * @return Allocated FPU buffer
 */
void* arch_fpu_alloc_area();

/**
 * @brief Frees the buffer returned by arch_fpu_alloc_area
 */
void arch_fpu_free_area(void* area);

/**
 * @brief Gets the size of an FPU save area
 * @return FPU save area size in bytes
 */
size_t arch_fpu_area_size();
