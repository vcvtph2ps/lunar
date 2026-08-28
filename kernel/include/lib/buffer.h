#pragma once
#include <memory/heap.h>

typedef struct {
    uint8_t* data;
    size_t size;
    size_t capacity;
} buffer_t;

/**
 * @brief Creates a new buffer with the specified initial capacity
 * @param capacity The initial capacity of the buffer in bytes
 * @return A pointer to the newly created buffer, or NULL on failure
 */
buffer_t* buffer_create(size_t capacity);

/**
 * @brief Frees the memory associated with the buffer
 * @param buffer A pointer to the buffer to free
 */
void buffer_free(buffer_t* buffer);

/**
 * @brief Appends data to the end of the buffer, resizing if necessary
 * @param buffer A pointer to the buffer to append to
 * @param data A pointer to the data to append
 * @param size The size of the data to append in bytes
 */
void buffer_append(buffer_t* buffer, const void* data, size_t size);

/**
 * @brief Slices the buffer to contain only the data from the specified offset and size
 * @param buffer A pointer to the buffer to slice
 * @param offset The offset in bytes from the start of the buffer to begin the slice
 * @param size The size of the slice in bytes
 * @note This function does not free the memory of the sliced-out portion, but simply adjusts the buffer's data pointer and size.
 */
void buffer_slice(buffer_t* buffer, size_t offset, size_t size);

/**
 * @brief Clears the buffer's data and resets its size to zero, but does not free the underlying memory
 * @param buffer A pointer to the buffer to clear
 */
void buffer_clear(buffer_t* buffer);
