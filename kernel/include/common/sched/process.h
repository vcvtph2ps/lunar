#pragma once
#include <stdint.h>

/**
 * @brief Allocates a new process ID.
 * @return A unique process ID.
 */
uint32_t process_allocate_id();

/**
 * @brief Frees a previously allocated process ID.
 * @param id The process ID to free.
 */
void process_free_id(uint32_t id);
