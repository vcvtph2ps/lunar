#pragma once

#include <common/assert.h>
#include <lib/math.h>
#include <memory/pmm.h>
#include <memory/slab.h>

/**
 * @brief Allocates a block of memory of the given size on the heap.
 * @param size The size of the block to allocate, in bytes.
 * @return a pointer to the allocated block, or nullptr if the allocation failed.
 */
void* heap_alloc(size_t size);

/**
 * @brief Reallocates a block of memory on the heap to a new size.
 * @param address The address of the block to reallocate, or nullptr to allocate a new block.
 * @param current_size The current size of the block, in bytes. This is required to correctly copy the existing data to the new block.
 * @param new_size The new size of the block, in bytes.
 * @return a pointer to the reallocated block, or nullptr if the reallocation failed.
 */
void* heap_realloc(void* address, size_t current_size, size_t new_size);

/**
 * @brief Reallocates a block of memory on the heap to a new size, with overflow checking for the total size of the allocation.
 * @param array The address of the block to reallocate, or nullptr to allocate a new array.
 * @param element_size The size of each element in the array, in bytes.
 * @param current_count The current number of elements in the array. This is required to correctly copy the existing data to the new block.
 * @param new_count The new number of elements in the array.
 * @return a pointer to the reallocated block, or nullptr if the reallocation failed or if the total size of the allocation would overflow.
 */
void* heap_reallocarray(void* array, size_t element_size, size_t current_count, size_t new_count);

/**
 * @brief Frees a block of memory on the heap that was previously allocated with heap_alloc or heap_realloc.
 * @param address The address of the block to free.
 * @param size The size of the block to free, in bytes.
 */
void heap_free(void* address, size_t size);

/**
 * @brief Initializes the heap allocator. This function must be called before any other heap functions are used, and should only be called once.
 */
void heap_init();
