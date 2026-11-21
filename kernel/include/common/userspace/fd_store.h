#pragma once
#include <common/fs/vfs.h>
#include <common/sync/spinlock.h>
#include <lib/bitmap.h>
#include <stdint.h>

typedef struct {
    bool read  : 1;
    bool write : 1;
} fd_store_access_t;

typedef struct {
    bool non_blocking : 1;
    bool append       : 1;
} fd_store_flags_t;

typedef struct {
    ATOMIC size_t refcount;
    vfs_node_t* node;
    size_t offset;

    fd_store_access_t access;
    fd_store_flags_t flags;
} fd_store_file_t;

typedef struct {
    fd_store_file_t* file;
    bool close_on_execute;
} fd_store_entry_t;

typedef struct {
    spinlock_t lock;

    bitmap_t* bitmap;
    fd_store_entry_t* fds;
    size_t fds_elements_count;
} fd_store_t;

/// @brief Creates a new empty file descriptor store
fd_store_t* fd_store_create();

/// @brief Frees a file descriptor store
/// @note Also frees all file descriptors currently in the store
void fd_store_free(fd_store_t* store);

/// @brief Clones a file descriptor store
fd_store_t* fd_store_clone(fd_store_t* parent_store);

/// @brief Closes all file descriptors marked as close on execute
void fd_store_handle_execute(fd_store_t* store);

/// @brief Create a new file descriptor, allocating a free fd number
/// @return The file descriptor
uint32_t fd_store_create_fd(fd_store_t* store, vfs_node_t* node, fd_store_access_t access, fd_store_flags_t flags, bool close_on_execute);

/// @brief Create a new file descriptor, at a free fd number
/// @return The file descriptor
uint32_t fd_store_create_fd_at(fd_store_t* store, vfs_node_t* node, uint32_t fd, fd_store_access_t access, fd_store_flags_t flags);

/// @brief Free an open file descriptor
bool fd_store_free_fd(fd_store_t* store, uint32_t fd);

/// @brief Get a file decriptor by it's number
/// @param out_close_on_execute (optional) whether the file descriptor was marked as close on execute
/// @note Increments the refrence count
fd_store_file_t* fd_store_get_fd(fd_store_t* store, uint32_t fd, bool* out_close_on_execute);

/// @brief Increment the reference count of a fd_store_file_t
/// @return The same file
fd_store_file_t* fd_store_file_get(fd_store_file_t* file);

/// @brief Decrement the reference count of a fd_store_file_t, freeing the file if it hits 0
void fd_store_file_put(fd_store_file_t* file);

/// @brief Get just the access of a file descriptor
vfs_result_t fd_store_get_access(fd_store_t* store, uint32_t fd, fd_store_access_t* out_access);

/// @brief Duplicate a file descriptor, allocating a free fd number
vfs_result_t fd_store_duplicate(fd_store_t* store, uint32_t fd, uint32_t* out_new_fd);

/// @brief Duplicate a file descriptor, at a free fd number
vfs_result_t fd_store_duplicate_to(fd_store_t* store, uint32_t fd, uint32_t target, bool close_on_execute);
