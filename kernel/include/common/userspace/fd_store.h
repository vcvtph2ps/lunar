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

typedef struct fd_store_file fd_store_file_t;
typedef struct fd_store_file_ops fd_store_file_ops_t;

/// @brief The backend kind of a file descriptor
typedef enum : uint8_t {
    /// @brief Backed by a vfs node (regular file, directory, device, ...)
    FD_STORE_FILE_TYPE_VFS_NODE,
    /// @brief Backed by an unnamed pipe
    FD_STORE_FILE_TYPE_PIPE
} fd_store_file_type_t;

/**
 * @brief Operations implemented by a file descriptor backend
 * @note Any operation may be nullptr if the backend does not support it
 */
struct fd_store_file_ops {
    /// @brief Performs IO on the file
    vfs_result_t (*perform_io)(fd_store_file_t* file, io_request_t* request);

    /// @brief Gets the attributes of the file
    vfs_result_t (*get_attributes)(fd_store_file_t* file, vfs_node_attr_t* out_attr);

    /// @brief Resizes the file
    vfs_result_t (*truncate)(fd_store_file_t* file, size_t size);

    /// @brief Gets the inode backing the file for memory mapping
    vfs_inode_t* (*get_inode)(fd_store_file_t* file);

    /// @brief Reads the next entry in a directory file
    vfs_result_t (*read_dir)(fd_store_file_t* file, size_t* offset, vfs_dentry_t** out_dentry);

    /// @brief Gets the vfs node backing the file, or nullptr if the file is not vfs backed
    /// @note Returns a borrowed pointer, the file's reference keeps the node alive
    vfs_node_t* (*get_node)(fd_store_file_t* file);

    /// @brief Releases backend's private state when the last reference to the file is dropped
    void (*release)(fd_store_file_t* file);
};

/// @brief The state shared by all file descriptors referring to the same open file
struct fd_store_file {
    ATOMIC size_t refcount;

    /// The operations for this file's backend
    const fd_store_file_ops_t* ops;

    /// The kind of backend this file is backed by
    fd_store_file_type_t type;

    /// The current file offset
    size_t offset;

    fd_store_access_t access;
    fd_store_flags_t flags;

    /// Backend private state
    union {
        vfs_node_t* node;
        void* data;
    };
};

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

/// @brief Closes every file descriptor in the store, releasing their references
void fd_store_close_all(fd_store_t* store);

/// @brief Creates a new file object with a custom backend
/// @param data Backend-private state, released through the backend's release operation
/// @return A file with a reference count of 1
fd_store_file_t* fd_store_file_create(fd_store_file_type_t type, const fd_store_file_ops_t* ops, void* data, fd_store_access_t access, fd_store_flags_t flags);

/// @brief Creates a new file object backed by a vfs node
/// @return A file with a reference count of 1, owning a reference to node
fd_store_file_t* fd_store_file_create_vfs_node(vfs_node_t* node, fd_store_access_t access, fd_store_flags_t flags);

/// @brief Insert an already-created file into the store at a free fd number
/// @return The file descriptor
/// @note Takes ownership of the caller's reference on success
uint32_t fd_store_insert_file(fd_store_t* store, fd_store_file_t* file, bool close_on_execute);

/// @brief Insert an already-created file into the store at a specific fd number
/// @return The file descriptor
/// @note Takes ownership of the caller's reference on success
uint32_t fd_store_insert_file_at(fd_store_t* store, fd_store_file_t* file, uint32_t fd);

/// @brief Free an open file descriptor
bool fd_store_free_fd(fd_store_t* store, uint32_t fd);

/// @brief Get a file descriptor by its number
/// @param out_close_on_execute (optional) whether the file descriptor was marked as close on execute
/// @note Increments the reference count
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

/// @brief Duplicate a file descriptor, at a given fd number
vfs_result_t fd_store_duplicate_to(fd_store_t* store, uint32_t fd, uint32_t target, bool close_on_execute);

/// @brief Performs IO on a file descriptor's file
vfs_result_t fd_store_file_perform_io(fd_store_file_t* file, io_request_t* request);

/// @brief Gets the attributes of a file descriptor's file
vfs_result_t fd_store_file_get_attributes(fd_store_file_t* file, vfs_node_attr_t* out_attr);

/// @brief Resizes a file descriptor's file
vfs_result_t fd_store_file_truncate(fd_store_file_t* file, size_t size);

/// @brief Gets the inode backing a file descriptor's file, or nullptr if it has none
vfs_inode_t* fd_store_file_get_inode(fd_store_file_t* file);

/// @brief Reads the next entry in a directory file descriptor's file
vfs_result_t fd_store_file_read_dir(fd_store_file_t* file, size_t* offset, vfs_dentry_t** out_dentry);

/// @brief Gets the vfs node backing a file descriptor's file, or nullptr if it is not vfs backed
/// @note Returns a borrowed pointer; the caller's file reference keeps the node alive
vfs_node_t* fd_store_file_get_node(fd_store_file_t* file);
