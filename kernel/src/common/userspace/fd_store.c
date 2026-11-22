#include <common/fs/vfs.h>
#include <common/sync/spinlock.h>
#include <common/userspace/fd_store.h>
#include <common/userspace/userspace.h>
#include <lib/helpers.h>
#include <lib/string.h>
#include <memory/heap.h>


static vfs_result_t fd_store_vfs_perform_io(fd_store_file_t* file, io_request_t* request) {
    return vfs_perform_io_node(file->node, request);
}

static vfs_result_t fd_store_vfs_get_attributes(fd_store_file_t* file, vfs_node_attr_t* out_attr) {
    return vfs_get_attributes_node(file->node, out_attr);
}

static vfs_result_t fd_store_vfs_truncate(fd_store_file_t* file, size_t size) {
    return vfs_truncate_node(file->node, size);
}

static vfs_inode_t* fd_store_vfs_get_inode(fd_store_file_t* file) {
    return vfs_node_get_inode(file->node);
}

static vfs_result_t fd_store_vfs_read_dir(fd_store_file_t* file, size_t* offset, vfs_dentry_t** out_dentry) {
    vfs_path_t path = VFS_MAKE_REL_PATH(file->node, "");
    return vfs_read_dir(&path, offset, out_dentry);
}

static vfs_node_t* fd_store_vfs_get_node(fd_store_file_t* file) {
    return file->node;
}

static void fd_store_vfs_release(fd_store_file_t* file) {
    vfs_node_put(file->node);
}

static const fd_store_file_ops_t g_fd_store_vfs_node_ops = {
    .perform_io = fd_store_vfs_perform_io,
    .get_attributes = fd_store_vfs_get_attributes,
    .truncate = fd_store_vfs_truncate,
    .get_inode = fd_store_vfs_get_inode,
    .read_dir = fd_store_vfs_read_dir,
    .get_node = fd_store_vfs_get_node,
    .release = fd_store_vfs_release,
};

fd_store_file_t* fd_store_file_create(fd_store_file_type_t type, const fd_store_file_ops_t* ops, void* data, fd_store_access_t access, fd_store_flags_t flags) {
    fd_store_file_t* file = heap_alloc(sizeof(fd_store_file_t));

    ATOMIC_STORE(&file->refcount, 1, ATOMIC_RELAXED);
    file->ops = ops;
    file->type = type;
    file->offset = 0;
    file->access = access;
    file->flags = flags;
    file->data = data;

    return file;
}

fd_store_file_t* fd_store_file_create_vfs_node(vfs_node_t* node, fd_store_access_t access, fd_store_flags_t flags) {
    return fd_store_file_create(FD_STORE_FILE_TYPE_VFS_NODE, &g_fd_store_vfs_node_ops, vfs_node_get(node), access, flags);
}

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
            if(file->ops != nullptr && file->ops->release != nullptr) {
                file->ops->release(file);
            }
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
        /// Release every file the store referenced
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


uint32_t fd_store_insert_file(fd_store_t* store, fd_store_file_t* file, bool close_on_execute) {
    spinlock_lock(&store->lock);

    size_t fd = bitmap_find_free(store->bitmap);
    if(fd == SIZE_MAX) {
        // @todo: reallocate
        user_assert(false && "fd_store out of size");
    }

    store->fds[fd].file = file;
    store->fds[fd].close_on_execute = close_on_execute;

    bitmap_set(store->bitmap, true, fd);
    spinlock_unlock(&store->lock);

    return fd;
}

uint32_t fd_store_insert_file_at(fd_store_t* store, fd_store_file_t* file, uint32_t fd) {
    spinlock_lock(&store->lock);

    user_assert(fd < store->fds_elements_count && "fd out of range");
    // @todo: we shouldn't assert here?
    user_assert(!bitmap_get(store->bitmap, fd) && "fd already in use");

    store->fds[fd].file = file;
    store->fds[fd].close_on_execute = false;

    bitmap_set(store->bitmap, true, fd);
    spinlock_unlock(&store->lock);

    return fd;
}

fd_store_file_t* fd_store_get_fd(fd_store_t* store, uint32_t fd, bool* out_close_on_execute) {
    spinlock_lock(&store->lock);

    bool valid = fd < store->fds_elements_count && bitmap_get(store->bitmap, fd);
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

syscall_ret_t fd_store_get_access(fd_store_t* store, uint32_t fd, fd_store_access_t* out_access) {
    spinlock_lock(&store->lock);

    if(fd >= store->fds_elements_count || !bitmap_get(store->bitmap, fd)) {
        spinlock_unlock(&store->lock);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADF);
    }

    *out_access = store->fds[fd].file->access;
    spinlock_unlock(&store->lock);
    return SYSCALL_RET_VALUE(0);
}

/// @brief Removes a file descriptor from the store
/// @return The file that was removed, or nullptr
/// @note the files reference must be dropped once the store lock is released
/// @note The caller must hold store->lock
static fd_store_file_t* fd_store_free_fd_lockless(fd_store_t* store, uint32_t fd) {
    if(fd >= store->fds_elements_count || !bitmap_get(store->bitmap, fd)) {
        return nullptr;
    }

    fd_store_file_t* file = store->fds[fd].file;
    store->fds[fd].file = nullptr;
    bitmap_set(store->bitmap, false, fd);
    return file;
}

bool fd_store_free_fd(fd_store_t* store, uint32_t fd) {
    spinlock_lock(&store->lock);
    fd_store_file_t* file = fd_store_free_fd_lockless(store, fd);
    spinlock_unlock(&store->lock);

    if(file == nullptr) {
        return false;
    }

    fd_store_file_put(file);
    return true;
}

syscall_ret_t fd_store_duplicate(fd_store_t* store, uint32_t fd) {
    return fd_store_duplicate_from(store, fd, 0, false);
}

syscall_ret_t fd_store_duplicate_from(fd_store_t* store, uint32_t fd, int32_t min_fd, bool close_on_execute) {
    if(min_fd < 0) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);
    }

    spinlock_lock(&store->lock);

    if(fd >= store->fds_elements_count || !bitmap_get(store->bitmap, fd)) {
        spinlock_unlock(&store->lock);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADF);
    }

    if((size_t) min_fd >= store->fds_elements_count) {
        spinlock_unlock(&store->lock);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);
    }

    size_t new_fd = SIZE_MAX;
    for(size_t i = (size_t) min_fd; i < store->fds_elements_count; i++) {
        if(!bitmap_get(store->bitmap, i)) {
            new_fd = i;
            break;
        }
    }

    if(new_fd == SIZE_MAX) {
        spinlock_unlock(&store->lock);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_MFILE);
    }

    /// Share the open file description with the new descriptor
    fd_store_file_t* file = fd_store_file_get(store->fds[fd].file);
    store->fds[new_fd].file = file;
    store->fds[new_fd].close_on_execute = close_on_execute;

    bitmap_set(store->bitmap, true, new_fd);
    spinlock_unlock(&store->lock);

    return SYSCALL_RET_VALUE(new_fd);
}

syscall_ret_t fd_store_duplicate_to(fd_store_t* store, uint32_t fd, uint32_t target, bool close_on_execute) {
    spinlock_lock(&store->lock);

    if(fd >= store->fds_elements_count || !bitmap_get(store->bitmap, fd)) {
        spinlock_unlock(&store->lock);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADF);
    }

    if(target >= store->fds_elements_count) {
        spinlock_unlock(&store->lock);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADF);
    }

    if(fd == target) {
        /// dup2() leaves the descriptor untouched when both numbers are equal
        spinlock_unlock(&store->lock);
        return SYSCALL_RET_VALUE(0);
    }

    /// Share the open file description with the target descriptor
    fd_store_file_t* entry = fd_store_file_get(store->fds[fd].file);
    fd_store_file_t* replaced = fd_store_free_fd_lockless(store, target);

    bitmap_set(store->bitmap, true, target);
    store->fds[target].file = entry;
    store->fds[target].close_on_execute = close_on_execute;

    spinlock_unlock(&store->lock);

    if(replaced != nullptr) {
        fd_store_file_put(replaced);
    }

    return SYSCALL_RET_VALUE(0);
}

syscall_ret_t fd_store_get_close_on_execute(fd_store_t* store, uint32_t fd, bool* out_close_on_execute) {
    spinlock_lock(&store->lock);

    if(fd >= store->fds_elements_count || !bitmap_get(store->bitmap, fd)) {
        spinlock_unlock(&store->lock);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADF);
    }

    *out_close_on_execute = store->fds[fd].close_on_execute;
    spinlock_unlock(&store->lock);
    return SYSCALL_RET_VALUE(0);
}

syscall_ret_t fd_store_set_close_on_execute(fd_store_t* store, uint32_t fd, bool close_on_execute) {
    spinlock_lock(&store->lock);

    if(fd >= store->fds_elements_count || !bitmap_get(store->bitmap, fd)) {
        spinlock_unlock(&store->lock);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADF);
    }

    store->fds[fd].close_on_execute = close_on_execute;
    spinlock_unlock(&store->lock);
    return SYSCALL_RET_VALUE(0);
}

syscall_ret_t fd_store_get_status_flags(fd_store_t* store, uint32_t fd, fd_store_access_t* out_access, fd_store_flags_t* out_flags) {
    spinlock_lock(&store->lock);

    if(fd >= store->fds_elements_count || !bitmap_get(store->bitmap, fd)) {
        spinlock_unlock(&store->lock);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADF);
    }

    *out_access = store->fds[fd].file->access;
    *out_flags = store->fds[fd].file->flags;
    spinlock_unlock(&store->lock);
    return SYSCALL_RET_VALUE(0);
}

syscall_ret_t fd_store_set_status_flags(fd_store_t* store, uint32_t fd, fd_store_flags_t flags) {
    spinlock_lock(&store->lock);

    if(fd >= store->fds_elements_count || !bitmap_get(store->bitmap, fd)) {
        spinlock_unlock(&store->lock);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADF);
    }

    store->fds[fd].file->flags = flags;
    spinlock_unlock(&store->lock);
    return SYSCALL_RET_VALUE(0);
}

void fd_store_handle_execute(fd_store_t* store) {
    if(store->fds == nullptr) {
        return;
    }

    for(size_t i = 0; i < store->fds_elements_count; i++) {
        spinlock_lock(&store->lock);

        fd_store_file_t* file = nullptr;
        if(bitmap_get(store->bitmap, i) && store->fds[i].file != nullptr && store->fds[i].close_on_execute) {
            LOG_KTRC("closing cloexec fd %ld\n", i);
            file = fd_store_free_fd_lockless(store, i);
        }

        spinlock_unlock(&store->lock);

        if(file != nullptr) {
            fd_store_file_put(file);
        }
    }
}

void fd_store_close_all(fd_store_t* store) {
    if(store == nullptr || store->fds == nullptr) {
        return;
    }

    for(size_t i = 0; i < store->fds_elements_count; i++) {
        spinlock_lock(&store->lock);

        fd_store_file_t* file = nullptr;
        if(bitmap_get(store->bitmap, i) && store->fds[i].file != nullptr) {
            file = fd_store_free_fd_lockless(store, i);
        }

        spinlock_unlock(&store->lock);

        if(file != nullptr) {
            fd_store_file_put(file);
        }
    }
}

vfs_result_t fd_store_file_perform_io(fd_store_file_t* file, io_request_t* request) {
    if(file == nullptr || file->ops == nullptr || file->ops->perform_io == nullptr) {
        return VFS_RESULT_ERR_UNSUPPORTED;
    }
    return file->ops->perform_io(file, request);
}

vfs_result_t fd_store_file_get_attributes(fd_store_file_t* file, vfs_node_attr_t* out_attr) {
    if(file == nullptr || file->ops == nullptr || file->ops->get_attributes == nullptr) {
        return VFS_RESULT_ERR_UNSUPPORTED;
    }
    return file->ops->get_attributes(file, out_attr);
}

vfs_result_t fd_store_file_truncate(fd_store_file_t* file, size_t size) {
    if(file == nullptr || file->ops == nullptr || file->ops->truncate == nullptr) {
        return VFS_RESULT_ERR_UNSUPPORTED;
    }
    return file->ops->truncate(file, size);
}

vfs_inode_t* fd_store_file_get_inode(fd_store_file_t* file) {
    if(file == nullptr || file->ops == nullptr || file->ops->get_inode == nullptr) {
        return nullptr;
    }
    return file->ops->get_inode(file);
}

vfs_result_t fd_store_file_read_dir(fd_store_file_t* file, size_t* offset, vfs_dentry_t** out_dentry) {
    if(file == nullptr || file->ops == nullptr || file->ops->read_dir == nullptr) {
        return VFS_RESULT_ERR_NOT_DIR;
    }
    return file->ops->read_dir(file, offset, out_dentry);
}

vfs_node_t* fd_store_file_get_node(fd_store_file_t* file) {
    if(file == nullptr || file->ops == nullptr || file->ops->get_node == nullptr) {
        return nullptr;
    }
    return file->ops->get_node(file);
}
