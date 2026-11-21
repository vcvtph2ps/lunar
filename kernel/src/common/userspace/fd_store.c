#include <common/fs/vfs.h>
#include <common/sync/spinlock.h>
#include <common/userspace/fd_store.h>
#include <common/userspace/userspace.h>
#include <lib/helpers.h>
#include <lib/string.h>
#include <memory/heap.h>


fd_store_file_t* fd_store_file_get(fd_store_file_t* file) {
    if(file != nullptr) {
        ATOMIC_LOAD_ADD(&file->refcount, 1, ATOMIC_ACQUIRE);
    }
    return file;
}

void fd_store_file_put(fd_store_file_t* file) {
    if(file != nullptr) {
        size_t previous = ATOMIC_LOAD_SUB(&file->refcount, 1, ATOMIC_RELEASE);
        if(previous == 1) {
            vfs_node_put(file->node);
            heap_free(file, sizeof(fd_store_file_t));
        }
    }
}

fd_store_t* fd_store_create() {
    fd_store_t* store = heap_alloc(sizeof(fd_store_t));
    store->lock = SPINLOCK_INIT;
    store->fds_elements_count = 64;
    store->fds = heap_reallocarray(nullptr, sizeof(fd_store_entry_t), 0, store->fds_elements_count);
    store->bitmap = bitmap_create(store->fds_elements_count);
    return store;
}

void fd_store_free(fd_store_t* store) {
    if(store->fds != nullptr) {
        /// Release every node the store referenced
        for(size_t i = 0; i < store->fds_elements_count; i++) {
            if(bitmap_get(store->bitmap, i) && store->fds[i].file != nullptr) {
                fd_store_file_put(store->fds[i].file);
            }
        }
        heap_free(store->fds, store->fds_elements_count * sizeof(fd_store_entry_t));
    }
    bitmap_free(store->bitmap);
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

        /// Bump file refcount
        fd_store_file_get(parent_store->fds[i].file);
        memory_copy(&store->fds[i], &parent_store->fds[i], sizeof(fd_store_entry_t));
    }

    spinlock_unlock(&parent_store->lock);
    return store;
}


uint32_t fd_store_create_fd(fd_store_t* store, vfs_node_t* node, fd_store_access_t access, fd_store_flags_t flags, bool close_on_execute) {
    spinlock_lock(&store->lock);

    size_t fd = bitmap_find_free(store->bitmap);
    if(fd == SIZE_MAX) {
        // @todo: reallocate
        user_assert(false && "fd_store out of size");
    }

    fd_store_file_t* file = heap_alloc(sizeof(fd_store_file_t));
    ATOMIC_STORE(&file->refcount, 1, ATOMIC_RELAXED);
    file->node = vfs_node_get(node);
    file->offset = 0;
    file->access = access;
    file->flags = flags;
    store->fds[fd].file = file;
    store->fds[fd].close_on_execute = close_on_execute;

    bitmap_set(store->bitmap, true, fd);
    spinlock_unlock(&store->lock);

    return fd;
}

uint32_t fd_store_create_fd_at(fd_store_t* store, vfs_node_t* node, uint32_t fd, fd_store_access_t access, fd_store_flags_t flags) {
    spinlock_lock(&store->lock);

    user_assert(fd < store->fds_elements_count && "fd out of range");
    // @todo: we shouldn't assert here?
    user_assert(!bitmap_get(store->bitmap, fd) && "fd already in use");

    fd_store_file_t* file = heap_alloc(sizeof(fd_store_file_t));

    ATOMIC_STORE(&file->refcount, 1, ATOMIC_RELAXED);
    file->node = vfs_node_get(node);
    file->offset = 0;
    file->access = access;
    file->flags = flags;
    store->fds[fd].file = file;
    store->fds[fd].close_on_execute = false;

    bitmap_set(store->bitmap, true, fd);
    spinlock_unlock(&store->lock);

    return fd;
}

fd_store_file_t* fd_store_get_fd(fd_store_t* store, uint32_t fd, bool* out_close_on_execute) {
    spinlock_lock(&store->lock);

    bool valid = bitmap_get(store->bitmap, fd);
    if(!valid) {
        spinlock_unlock(&store->lock);
        return nullptr;
    }

    if(out_close_on_execute != nullptr)
        *out_close_on_execute = store->fds[fd].close_on_execute;

    fd_store_file_t* file = store->fds[fd].file;
    fd_store_file_get(file);
    spinlock_unlock(&store->lock);
    return file;
}

vfs_result_t fd_store_get_access(fd_store_t* store, uint32_t fd, fd_store_access_t* out_access) {
    spinlock_lock(&store->lock);

    if(fd >= store->fds_elements_count || !bitmap_get(store->bitmap, fd)) {
        spinlock_unlock(&store->lock);
        return VFS_RESULT_ERR_NOT_FOUND;
    }

    *out_access = store->fds[fd].file->access;
    spinlock_unlock(&store->lock);
    return VFS_RESULT_OK;
}

static vfs_result_t fd_store_free_fd_lockless(fd_store_t* store, uint32_t fd) {
    bool valid = bitmap_get(store->bitmap, fd);
    if(!valid) {
        spinlock_unlock(&store->lock);
        return false;
    }

    fd_store_entry_t* node = &store->fds[fd];
    if(node->file == nullptr) {
        bitmap_set(store->bitmap, false, fd);
        spinlock_unlock(&store->lock);
        return false;
    }

    fd_store_file_put(node->file);
    bitmap_set(store->bitmap, false, fd);
    return VFS_RESULT_OK;
}

bool fd_store_free_fd(fd_store_t* store, uint32_t fd) {
    spinlock_lock(&store->lock);
    bool res = fd_store_free_fd_lockless(store, fd);
    spinlock_unlock(&store->lock);
    return res;
}

vfs_result_t fd_store_duplicate(fd_store_t* store, uint32_t fd, uint32_t* out_new_fd) {
    // @note: we "leak" the ref here, since we do the deref on close
    fd_store_file_t* file = fd_store_get_fd(store, fd, nullptr);
    if(file == nullptr) {
        return VFS_RESULT_ERR_NOT_FOUND;
    }

    spinlock_lock(&store->lock);

    size_t new_fd = bitmap_find_free(store->bitmap);
    if(new_fd == SIZE_MAX) {
        // @todo: reallocate
        user_assert(false && "fd_store out of size");
    }

    store->fds[new_fd].file = file;
    store->fds[new_fd].close_on_execute = false; // POSIX dup clears cloexec

    bitmap_set(store->bitmap, true, new_fd);
    spinlock_unlock(&store->lock);

    *out_new_fd = (uint32_t) new_fd;
    return VFS_RESULT_OK;
}

vfs_result_t fd_store_duplicate_to(fd_store_t* store, uint32_t fd, uint32_t target, bool close_on_execute) {
    // @note: we "leak" the ref here, since we do the deref on close
    fd_store_file_t* entry = fd_store_get_fd(store, fd, nullptr);
    if(entry == nullptr) {
        return VFS_RESULT_ERR_NOT_FOUND;
    }

    spinlock_lock(&store->lock);
    if(fd != target) {
        fd_store_free_fd_lockless(store, target);
    }

    bitmap_set(store->bitmap, true, target);

    store->fds[target].file = entry;
    if(fd == target) {
        store->fds[target].close_on_execute = false;
    } else {
        store->fds[target].close_on_execute = close_on_execute;
    }

    spinlock_unlock(&store->lock);

    if(fd == target) {
        fd_store_file_put(entry);
    }

    return VFS_RESULT_OK;
}

void fd_store_handle_execute(fd_store_t* store) {
    if(store->fds != nullptr) {
        for(size_t i = 0; i < store->fds_elements_count; i++) {
            if(bitmap_get(store->bitmap, i) && store->fds[i].file != nullptr) {
                if(store->fds[i].close_on_execute) {
                    LOG_KTRC("closing cloexec fd %ld\n", i);
                    fd_store_free_fd_lockless(store, i);
                }
            }
        }
    }
}
