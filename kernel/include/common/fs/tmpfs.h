#pragma once
#include <common/fs/vfs.h>
#include <stddef.h>
#include <stdint.h>

/// @brief A file to create when a tmpfs instance is mounted.
typedef struct {
    const char* name;
    const void* data;
    size_t size;
} tmpfs_mount_file_t;

/// @brief The private data passed to vfs_mount() when mounting a tmpfs.
typedef struct {
    const tmpfs_mount_file_t* files;
    size_t file_count;
} tmpfs_mount_data_t;

/**
 * @brief Adds an file to an already mounted tmpfs instance.
 * @param vfs The mounted tmpfs instance.
 * @param name The name of the new file in the filesystem root.
 * @param data The contents of the file.
 * @param size The size of the file in bytes.
 */
vfs_result_t tmpfs_add_file(vfs_t* vfs, const char* name, const void* data, size_t size);
