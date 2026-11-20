#pragma once

#include <stddef.h>

/**
 * @brief Decompresses a single zstd frame into a freshly allocated buffer.
 * @param src Compressed frame
 * @param src_size Size of the compressed frame in bytes
 * @param buffer_size Receives the size of the allocated output buffer, or NULL
 * @return The decompressed buffer, or nullptr on failure
 */
void* zstd_decompress(const void* src, size_t src_size, size_t* buffer_size);