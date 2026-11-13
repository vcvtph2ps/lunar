#include <common/fs/io.h>
#include <common/fs/vfs_inode.h>
#include <lib/string.h>
#include <memory/heap.h>
#include <memory/page_cache.h>
#include <memory/ptm.h>
#include <memory/vm.h>

static bool inode_cache_fill(void* context, uint64_t index, void* dest) {
    vfs_inode_t* inode = context;
    memory_zero(dest, PAGE_SIZE_DEFAULT);

    if(inode->ops->read_page == nullptr)
        return false;

    return inode->ops->read_page(inode, index, dest);
}

static bool inode_cache_writeback(void* context, uint64_t index, const void* src) {
    vfs_inode_t* inode = context;

    if(inode->ops->write_page == nullptr)
        return true;

    return inode->ops->write_page(inode, index, src);
}

vfs_inode_t* vfs_inode_create(const vfs_inode_ops_t* ops, void* private_data, size_t size) {
    vfs_inode_t* inode = heap_alloc(sizeof(vfs_inode_t));
    if(inode == nullptr)
        return nullptr;

    memory_zero(inode, sizeof(vfs_inode_t));
    inode->ops = ops;
    inode->private_data = private_data;
    inode->size = size;
    ATOMIC_STORE(&inode->refcount, 1, ATOMIC_RELAXED);

    /// The page cache lives with the inode for its whole lifetime;
    /// @note read only inodes simply get no writeback callback
    inode->cache = page_cache_object_create(inode, inode_cache_fill, ops->write_page != nullptr ? inode_cache_writeback : nullptr, size);
    return inode;
}

vfs_inode_t* vfs_inode_get(vfs_inode_t* inode) {
    if(inode != nullptr)
        ATOMIC_LOAD_ADD(&inode->refcount, 1, ATOMIC_SEQ_CST);
    return inode;
}

void vfs_inode_put(vfs_inode_t* inode) {
    if(inode == nullptr)
        return;

    if(ATOMIC_LOAD_SUB(&inode->refcount, 1, ATOMIC_SEQ_CST) == 1) {
        if(inode->cache != nullptr)
            page_cache_object_destroy(inode->cache);
        heap_free(inode, sizeof(vfs_inode_t));
    }
}

static bool inode_read_page(vfs_inode_t* inode, uint64_t index, void* buffer, size_t offset, size_t count) {
    page_cache_page_t* page = page_cache_get(inode->cache, index);
    if(page == nullptr)
        return false;

    memory_copy(buffer, (uint8_t*) PTM_TO_HHDM(page->physical) + offset, count);
    page_cache_put(page);
    return true;
}

bool vfs_inode_read(vfs_inode_t* inode, size_t offset, io_buffer_t* buffer, size_t count, size_t* out_read) {
    if(out_read != nullptr)
        *out_read = 0;

    if(inode == nullptr || buffer == nullptr)
        return false;

    if(offset >= inode->size || count == 0)
        return true;

    if(count > inode->size - offset)
        count = inode->size - offset;

    size_t done = 0;
    while(done < count) {
        size_t absolute = offset + done;
        uint64_t index = absolute / PAGE_SIZE_DEFAULT;
        size_t page_offset = absolute % PAGE_SIZE_DEFAULT;

        size_t chunk = PAGE_SIZE_DEFAULT - page_offset;
        if(chunk > count - done)
            chunk = count - done;

        // @todo: user buffers
        if(!inode_read_page(inode, index, (uint8_t*) buffer->buffer + done, page_offset, chunk))
            return false;

        done += chunk;
    }

    if(out_read != nullptr)
        *out_read = done;
    return true;
}

bool vfs_inode_resize(vfs_inode_t* inode, size_t new_size) {
    if(inode == nullptr || inode->ops == nullptr || inode->ops->resize == nullptr)
        return false;

    if(!inode->ops->resize(inode, new_size))
        return false;

    size_t old_size = inode->size;

    /// Commit the new size first so any fault past the new end is rejected immediately, even while we tear mappings down.
    inode->size = new_size;
    if(inode->cache != nullptr)
        inode->cache->size = new_size;

    /// Shrinking invalidates the page holding the new end and everything after it.
    if(new_size < old_size && inode->cache != nullptr) {
        uint64_t first_page = new_size / PAGE_SIZE_DEFAULT;
        uint64_t last_page = (old_size + PAGE_SIZE_DEFAULT - 1) / PAGE_SIZE_DEFAULT;

        /// Drop mappings at or past the new end first, so their ptes stop pinning the cached frames.
        vm_unmap_file_range(inode->cache, first_page * PAGE_SIZE_DEFAULT);

        /// Evicting those pages makes the page holding the new end refill with a zeroed tail.
        for(uint64_t index = first_page; index < last_page; index++) vfs_inode_mark_unreadable(inode, index);
    }

    return true;
}

bool vfs_inode_write(vfs_inode_t* inode, size_t offset, const io_buffer_t* buffer, size_t count, size_t* out_written) {
    if(out_written != nullptr)
        *out_written = 0;

    if(inode == nullptr || buffer == nullptr || inode->ops->write_page == nullptr)
        return false;

    if(count == 0)
        return true;

    if(offset > SIZE_MAX - count)
        return false;

    /// A write past the end of the file extends it to cover the written range.
    size_t end = offset + count;
    if(end > inode->size && !vfs_inode_resize(inode, end))
        return false;

    size_t done = 0;
    while(done < count) {
        size_t absolute = offset + done;
        uint64_t index = absolute / PAGE_SIZE_DEFAULT;
        size_t page_offset = absolute % PAGE_SIZE_DEFAULT;

        size_t chunk = PAGE_SIZE_DEFAULT - page_offset;
        if(chunk > count - done)
            chunk = count - done;

        /// Writes go through the cache and mark the page dirty, the filesystem only sees them when the page is written back.
        if(!page_cache_write(inode->cache, index, (const uint8_t*) buffer->buffer + done, page_offset, chunk))
            return false;

        done += chunk;
    }

    if(out_written != nullptr)
        *out_written = done;
    return true;
}

vfs_page_cache_object_t* vfs_inode_cache(vfs_inode_t* inode) {
    return inode != nullptr ? inode->cache : nullptr;
}

size_t vfs_inode_writeback(vfs_inode_t* inode) {
    return vfs_inode_cache(inode) != nullptr ? page_cache_writeback(inode->cache) : 0;
}

bool vfs_inode_mark_unreadable(vfs_inode_t* inode, uint64_t index) {
    return vfs_inode_cache(inode) != nullptr ? page_cache_mark_unreadable(inode->cache, index) : false;
}
