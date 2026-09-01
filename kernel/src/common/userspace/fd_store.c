#include <common/fs/vfs.h>
#include <common/sync/spinlock.h>
#include <common/userspace/fd_store.h>
#include <common/userspace/userspace.h>
#include <lib/helpers.h>
#include <memory/heap.h>

fd_store_t* fd_store_create() {
    fd_store_t* store = heap_alloc(sizeof(fd_store_t));
    store->lock = SPINLOCK_INIT;
    store->fds_elements_count = 64;
    store->fds = heap_reallocarray(nullptr, sizeof(fd_store_entry_t), 0, store->fds_elements_count);
    store->bitmap = bitmap_create(64);
    // @todo: don't do this
    for(int i = 0; i <= 10; i++) {
        bitmap_set(store->bitmap, true, i);
    }
    return store;
}

void fd_store_free(fd_store_t* store) {
    bitmap_free(store->bitmap);
    if(store->fds != nullptr) {
        heap_free(store->fds, store->fds_elements_count * sizeof(fd_store_entry_t));
    }
    heap_free(store, sizeof(fd_store_t));
}

uint32_t fd_store_create_fd(fd_store_t* store, vfs_node_t* node) {
    spinlock_lock(&store->lock);

    size_t fd = bitmap_find_free(store->bitmap);
    if(fd == SIZE_MAX) {
        // @todo: reallocate
        user_assert(false && "fd_store out of size");
    }

    store->fds[fd] = (fd_store_entry_t) { .node = vfs_node_get(node), .offset = 0 };
    bitmap_set(store->bitmap, true, fd);
    spinlock_unlock(&store->lock);
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
