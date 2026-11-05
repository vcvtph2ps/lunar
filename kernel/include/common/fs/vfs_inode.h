#pragma once
#include <lib/helpers.h>
#include <lib/types.h>
#include <memory/page_cache.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct vfs_inode vfs_inode_t; // NOLINT
typedef struct vfs_inode_ops vfs_inode_ops_t;

struct vfs_inode_ops {
    /**
     * @brief Populates one page of the file's contents.
     * @param index The zero based page index.
     * @param dest A PAGE_SIZE_DEFAULT byte buffer to fill only the bytes within the file need to be written.
     * @return true on success, false if the page could not be read.
     */
    bool (*read_page)(vfs_inode_t* inode, uint64_t index, void* dest);

    /**
     * @brief Writes one dirty page back to the backing store.
     * @param index The zero based page index.
     * @param src A PAGE_SIZE_DEFAULT byte buffer holding the page contents.
     * @return true on success, false if the page could not be written back.
     * @note nullptr for read only inodes.
     */
    bool (*write_page)(vfs_inode_t* inode, uint64_t index, const void* src);
};

/**
 * Unlike a vnode, which a filesystem may hand out new on every lookup, an inode is stable for as long as the file exists
 * the inode and its page cache are owned by the VFS, the fs only provides the ops and its own private data.
 * vnodes reference the inode and drop their reference when they are freed.
 */
struct vfs_inode {
    const vfs_inode_ops_t* ops;
    void* private_data;

    /// The size of the file in bytes.
    size_t size;

    /// the page cache backing the inode's contents.
    vfs_page_cache_object_t* cache;

    ATOMIC size_t refcount;
};

/**
 * @brief Creates a new inode with a reference owned by the caller
 * @return The new inode, or nullptr on allocation failure
 */
vfs_inode_t* vfs_inode_create(const vfs_inode_ops_t* ops, void* private_data, size_t size);

/**
 * @brief Retains a reference on an inode.
 */
vfs_inode_t* vfs_inode_get(vfs_inode_t* inode);

/**
 * @brief Releases a reference on an inode, destroying its page cache and freeing it once the last reference is dropped
 */
void vfs_inode_put(vfs_inode_t* inode);

/**
 * @brief Reads up to `count` bytes at `offset`, populating pages on demand
 * @param offset The byte offset to start reading at
 * @param buffer A buffer to read into
 * @param count The number of bytes to read
 * @param out_read Receives the number of bytes read
 * @return true on success, false if a page could not be populated
 */
bool vfs_inode_read(vfs_inode_t* inode, size_t offset, void* buffer, size_t count, size_t* out_read);

/**
 * @brief Writes up to `count` bytes at `offset` through the cache
 * @param offset The byte offset to start writing at
 * @param buffer The buffer to write from
 * @param count The number of bytes to write
 * @param out_written Receives the number of bytes written
 * @return true on success, false if the inode is read only or a page failed
 */
bool vfs_inode_write(vfs_inode_t* inode, size_t offset, const void* buffer, size_t count, size_t* out_written);

/**
 * @brief Returns the page cache object backing the inode
 */
vfs_page_cache_object_t* vfs_inode_cache(vfs_inode_t* inode);

/**
 * @brief Writes back every dirty page of the inode
 * @return The number of pages written back
 */
size_t vfs_inode_writeback(vfs_inode_t* inode);

/**
 * @brief Evicts one resident, unreferenced page of the inode, so the next access reloads it through read_page
 * @return true if the page was evicted, false if it was absent or still in use
 */
bool vfs_inode_mark_unreadable(vfs_inode_t* inode, uint64_t index);
