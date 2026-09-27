#include <common/fs/vfs.h>
#include <common/sync/spinlock.h>
#include <common/userspace/fd_store.h>
#include <common/userspace/userspace.h>
#include <lib/helpers.h>
#include <lib/string.h>
#include <memory/heap.h>

fd_store_t* fd_store_create() {
    fd_store_t* store = heap_alloc(sizeof(fd_store_t));
    store->lock = SPINLOCK_INIT;
    store->fds_elements_count = 64;
    store->fds = heap_reallocarray(nullptr, sizeof(fd_store_entry_t), 0, store->fds_elements_count);
    store->bitmap = bitmap_create(store->fds_elements_count);
    return store;
}

void fd_store_free(fd_store_t* store) {
    bitmap_free(store->bitmap);
    if(store->fds != nullptr) {
        heap_free(store->fds, store->fds_elements_count * sizeof(fd_store_entry_t));
    }
    heap_free(store, sizeof(fd_store_t));
}

fd_store_t* fd_store_clone(fd_store_t* parent_store) {
    spinlock_lock(&parent_store->lock);

    fd_store_t* store = heap_alloc(sizeof(fd_store_t));
    store->lock = SPINLOCK_INIT;
    store->fds_elements_count = parent_store->fds_elements_count;
    store->fds = heap_reallocarray(nullptr, sizeof(fd_store_entry_t), 0, parent_store->fds_elements_count);
    store->bitmap = bitmap_clone(parent_store->bitmap);

    for(size_t i = 0; i < parent_store->fds_elements_count; i++) {
        bool valid = bitmap_get(parent_store->bitmap, i);
        if(!valid) {
            continue;
        }

        /// Bump node refcount
        vfs_node_get(parent_store->fds[i].node);
        memory_copy(&store->fds[i], &parent_store->fds[i], sizeof(fd_store_entry_t));
    }

    spinlock_unlock(&parent_store->lock);
    return store;
}

uint32_t fd_store_create_fd(fd_store_t* store, vfs_node_t* node, fd_store_entry_t** out_entry) {
    spinlock_lock(&store->lock);

    size_t fd = bitmap_find_free(store->bitmap);
    if(fd == SIZE_MAX) {
        // @todo: reallocate
        user_assert(false && "fd_store out of size");
    }

    store->fds[fd].node = vfs_node_get(node);
    store->fds[fd].offset = 0;
    store->fds[fd].access.read = false;
    store->fds[fd].access.write = false;
    store->fds[fd].non_blocking = false;

    bitmap_set(store->bitmap, true, fd);
    spinlock_unlock(&store->lock);

    *out_entry = &store->fds[fd];
    return fd;
}

uint32_t fd_store_create_fd_at(fd_store_t* store, vfs_node_t* node, uint32_t fd, fd_store_entry_t** out_entry) {
    spinlock_lock(&store->lock);

    user_assert(fd < store->fds_elements_count && "fd out of range");
    user_assert(!bitmap_get(store->bitmap, fd) && "fd already in use");

    store->fds[fd].node = vfs_node_get(node);
    store->fds[fd].offset = 0;
    store->fds[fd].access.read = false;
    store->fds[fd].access.write = false;
    store->fds[fd].non_blocking = false;

    bitmap_set(store->bitmap, true, fd);
    spinlock_unlock(&store->lock);

    *out_entry = &store->fds[fd];
    return fd;
}

fd_store_entry_t* fd_store_get_fd(fd_store_t* store, uint32_t fd) {
    spinlock_lock(&store->lock);

    bool valid = bitmap_get(store->bitmap, fd);
    if(!valid) {
        spinlock_unlock(&store->lock);
        return nullptr;
    }

    spinlock_unlock(&store->lock);
    return &store->fds[fd];
}

bool fd_store_free_fd(fd_store_t* store, uint32_t fd) {
    spinlock_lock(&store->lock);

    bool valid = bitmap_get(store->bitmap, fd);
    if(!valid) {
        spinlock_unlock(&store->lock);
        return false;
    }

    fd_store_entry_t* node = &store->fds[fd];
    if(node->node == nullptr) {
        bitmap_set(store->bitmap, false, fd);
        spinlock_unlock(&store->lock);
        return false;
    }

    vfs_node_put(node->node);
    bitmap_set(store->bitmap, false, fd);

    spinlock_unlock(&store->lock);
    return true;
}
