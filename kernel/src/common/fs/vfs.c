#include <common/fs/dentry.h>
#include <common/fs/io.h>
#include <common/fs/vfs.h>
#include <common/sync/lock.h>
#include <common/sync/rwspinlock.h>
#include <lib/string.h>
#include <memory/heap.h>
#include <uapi/limits.h>

spinlock_t g_vfs_list_lock;
list_t g_vfs_list;

vfs_dentry_t* g_root_dentry;

static vfs_result_t lookup_component(vfs_dentry_t* current, const char* component, vfs_dentry_t** out_next) {
    if(string_compare(component, ".") == 0) {
        *out_next = vfs_dentry_get(current);
        return VFS_RESULT_OK;
    }

    if(string_compare(component, "..") == 0) {
        *out_next = current->parent != nullptr ? vfs_dentry_get(current->parent) : vfs_dentry_get(current);
        return VFS_RESULT_OK;
    }

    vfs_dentry_t* dentry = vfs_dcache_lookup(current, component);
    if(dentry != nullptr) {
        if(dentry->negative) {
            vfs_dentry_put(dentry);
            return VFS_RESULT_ERR_NOT_FOUND;
        }
        *out_next = dentry;
        return VFS_RESULT_OK;
    }

    rwspinlock_write_t* dir_lock = rwspinlock_lock_write(&current->rwspinlock);

    dentry = vfs_dcache_lookup(current, component);
    if(dentry != nullptr) {
        rwspinlock_unlock_write(dir_lock);
        if(dentry->negative) {
            vfs_dentry_put(dentry);
            return VFS_RESULT_ERR_NOT_FOUND;
        }
        *out_next = dentry;
        return VFS_RESULT_OK;
    }

    vfs_node_t* next_node;
    lock_acquire(&current->node->lock);
    vfs_result_t res = current->node->ops->lookup(current->node, component, &next_node);
    lock_release(&current->node->lock);

    if(res == VFS_RESULT_OK && next_node == nullptr)
        res = VFS_RESULT_ERR_NOT_FOUND;

    if(res == VFS_RESULT_ERR_NOT_FOUND) {
        res = vfs_dentry_create(current, component, nullptr, true, &dentry);
        if(res == VFS_RESULT_OK) {
            vfs_dcache_insert(dentry);
            res = VFS_RESULT_ERR_NOT_FOUND;
        }
    } else if(res == VFS_RESULT_OK) {
        res = vfs_dentry_create(current, component, next_node, false, &dentry);
        vfs_node_put(next_node);
        if(res == VFS_RESULT_OK) {
            vfs_dcache_insert(dentry);
            *out_next = vfs_dentry_get(dentry);
        }
    }

    rwspinlock_unlock_write(dir_lock);
    return res;
}

static vfs_dentry_t* dentry_for_node(vfs_node_t* node) {
    lock_acquire(&node->lock);
    vfs_dentry_t* dentry = nullptr;
    if(node->dentries.head != nullptr) {
        dentry = CONTAINER_OF(node->dentries.head, vfs_dentry_t, alias_node);
        vfs_dentry_get(dentry);
    }
    lock_release(&node->lock);

    if(dentry != nullptr)
        return dentry;

    vfs_node_t* root_node;
    if(vfs_root_node(&root_node) != VFS_RESULT_OK)
        return nullptr;

    if(root_node != node) {
        vfs_node_put(root_node);
        return nullptr;
    }

    vfs_node_put(root_node);
    return vfs_dentry_get(g_root_dentry);
}

vfs_node_t* vfs_node_get(vfs_node_t* node) {
    if(node != nullptr) {
        ATOMIC_LOAD_ADD(&node->refcount, 1, ATOMIC_SEQ_CST);
    }
    return node;
}

void vfs_node_put(vfs_node_t* node) {
    if(node != nullptr) {
        size_t previous = ATOMIC_LOAD_SUB(&node->refcount, 1, ATOMIC_SEQ_CST);
        if(previous == 1) {
            if(node->ops && node->ops->release) {
                node->ops->release(node);
            } else {
                heap_free(node, sizeof(vfs_node_t));
            }
        }
    }
}

vfs_result_t vfs_mount(const vfs_ops_t* ops, const vfs_path_t* mount_point, void* private_data) {
    vfs_t* vfs = heap_alloc(sizeof(vfs_t));

    vfs->ops = ops;
    vfs->private_data = private_data;

    spinlock_lock(&g_vfs_list_lock);
    if(g_vfs_list.count == 0) {
        assert(mount_point == nullptr);

        vfs->mount_point = nullptr;
        vfs_result_t res = vfs->ops->mount(vfs);
        if(res != VFS_RESULT_OK) {
            heap_free(vfs, sizeof(vfs_t));
            spinlock_unlock(&g_vfs_list_lock);
            return res;
        }

        list_push_back(&g_vfs_list, &vfs->global_list_node);
        spinlock_unlock(&g_vfs_list_lock);
        return VFS_RESULT_OK;
    }
    spinlock_unlock(&g_vfs_list_lock);

    vfs_dentry_t* dentry;
    vfs_result_t res = vfs_lookup_dentry(mount_point, true, &dentry);
    if(res != VFS_RESULT_OK) {
        heap_free(vfs, sizeof(vfs_t));
        return res;
    }

    rwspinlock_write_t* write_lock = rwspinlock_lock_write(&dentry->rwspinlock);

    if(dentry->node == nullptr || dentry->node->type != VFS_NODE_TYPE_DIR) {
        heap_free(vfs, sizeof(vfs_t));
        rwspinlock_unlock_write(write_lock);
        vfs_dentry_put(dentry);
        return VFS_RESULT_ERR_NOT_DIR;
    }

    if(dentry->mounted_vfs != nullptr) {
        heap_free(vfs, sizeof(vfs_t));
        rwspinlock_unlock_write(write_lock);
        vfs_dentry_put(dentry);
        return VFS_RESULT_ERR_EXISTS;
    }

    dentry->mounted_vfs = vfs;
    vfs->mount_point = dentry;
    vfs->ops->mount(vfs);
    rwspinlock_unlock_write(write_lock);

    spinlock_lock(&g_vfs_list_lock);
    list_push_back(&g_vfs_list, &vfs->global_list_node);
    spinlock_unlock(&g_vfs_list_lock);
    return VFS_RESULT_OK;
}

vfs_result_t vfs_unmount(const vfs_path_t* path) {
    (void) path;
    ASSERT_TODO();
}

vfs_result_t vfs_root_node(vfs_node_t** out_root_node) {
    spinlock_lock(&g_vfs_list_lock);
    if(g_vfs_list.count == 0) {
        spinlock_unlock(&g_vfs_list_lock);
        return VFS_RESULT_ERR_NOT_FOUND;
    }
    vfs_t* root_vfs = CONTAINER_OF(g_vfs_list.head, vfs_t, global_list_node);
    spinlock_unlock(&g_vfs_list_lock);

    vfs_node_t* root_node;
    vfs_result_t res = root_vfs->ops->get_root_node(root_vfs, &root_node);
    if(res != VFS_RESULT_OK)
        return res;

    if(g_root_dentry == nullptr) {
        vfs_dentry_t* root_dentry;
        res = vfs_dentry_create(nullptr, "", root_node, false, &root_dentry);
        if(res != VFS_RESULT_OK) {
            vfs_node_put(root_node);
            return res;
        }
        vfs_dcache_insert(root_dentry);
        vfs_dentry_get(root_dentry); // pinned for the kernel's lifetime
        g_root_dentry = root_dentry;
    }

    *out_root_node = root_node; // Transfers the reference to the caller
    return VFS_RESULT_OK;
}

vfs_result_t vfs_perform_io(const vfs_path_t* path, io_request_t* request) {
    vfs_node_t* node;
    vfs_result_t res = vfs_lookup(path, true, &node);
    if(res != VFS_RESULT_OK)
        return res;

    // @todo: read/write lock
    lock_acquire(&node->lock);
    res = node->ops->perform_io(node, request);
    lock_release(&node->lock);

    vfs_node_put(node);
    return res;
}

vfs_result_t vfs_perform_io_node(vfs_node_t* node, io_request_t* request) {
    vfs_node_get(node);
    vfs_result_t res;

    // @todo: read/write lock
    lock_acquire(&node->lock);
    res = node->ops->perform_io(node, request);
    lock_release(&node->lock);

    vfs_node_put(node);
    return res;
}

vfs_result_t vfs_get_attributes(const vfs_path_t* path, vfs_node_attr_t* out_attr) {
    vfs_node_t* node;
    vfs_result_t res = vfs_lookup(path, true, &node);
    if(res != VFS_RESULT_OK)
        return res;

    // @todo: read/write lock
    lock_acquire(&node->lock);
    res = node->ops->get_attributes(node, out_attr);
    lock_release(&node->lock);

    vfs_node_put(node);
    return res;
}

vfs_result_t vfs_get_attributes_node(vfs_node_t* node, vfs_node_attr_t* out_attr) {
    vfs_node_get(node);

    // @todo: read/write lock
    lock_acquire(&node->lock);
    vfs_result_t res = node->ops->get_attributes(node, out_attr);
    lock_release(&node->lock);

    vfs_node_put(node);
    return res;
}

static vfs_dentry_t* root_dentry(void) {
    vfs_node_t* root_node;
    if(vfs_root_node(&root_node) != VFS_RESULT_OK)
        return nullptr;

    vfs_dentry_t* dentry = dentry_for_node(root_node);
    vfs_node_put(root_node);
    return dentry;
}

static vfs_result_t read_link_target(vfs_node_t* node, char** out_buf, size_t* out_size) {
    lock_acquire(&node->lock);

    vfs_node_attr_t attr;
    vfs_result_t res = node->ops->get_attributes(node, &attr);
    if(res != VFS_RESULT_OK) {
        lock_release(&node->lock);
        return res;
    }

    size_t size = attr.size + 1;
    char* buf = heap_alloc(size);

    io_request_t io_req;
    io_req.type = IO_REQUEST_READ;
    io_req.read.buffer = buf;
    io_req.read.count = attr.size;
    io_req.read.offset = 0;
    io_req.read.bytes_read = 0;
    io_req.no_block = false;
    res = node->ops->perform_io(node, &io_req);
    lock_release(&node->lock);

    if(res != VFS_RESULT_OK) {
        heap_free(buf, size);
        return res;
    }

    buf[attr.size] = '\0';
    *out_buf = buf;
    *out_size = size;
    return VFS_RESULT_OK;
}

/// @brief Crosses any mounts stacked on `*inout`, leaving it at the inner-most root.
static vfs_result_t traverse_mounts(vfs_dentry_t** inout) {
    while((*inout)->mounted_vfs != nullptr) {
        vfs_dentry_t* current = *inout;

        vfs_dentry_t* mount_root = vfs_dcache_lookup(current, "");
        if(mount_root != nullptr) {
            vfs_dentry_put(current);
            *inout = mount_root;
            continue;
        }

        vfs_node_t* root_node;
        vfs_result_t res = current->mounted_vfs->ops->get_root_node(current->mounted_vfs, &root_node);
        if(res != VFS_RESULT_OK)
            return res;

        res = vfs_dentry_create(current, "", root_node, false, &mount_root);
        vfs_node_put(root_node);
        if(res != VFS_RESULT_OK)
            return res;

        vfs_dcache_insert(mount_root);
        vfs_dentry_get(mount_root); // the dentry cache's reference
        vfs_dentry_put(current);
        *inout = mount_root;
    }
    return VFS_RESULT_OK;
}

/// @brief Walks `path` from `current` (owned by this function), following symlinks in intermediate components and, when `follow_final` is set, in the final one too.
static vfs_result_t resolve_path(vfs_dentry_t* current, const char* path, bool follow_final, unsigned depth, vfs_dentry_t** out) {
    if(depth > UAPI_LIMITS_MAX_SYMLINKS) {
        vfs_dentry_put(current);
        return VFS_RESULT_ERR_LOOP;
    }

    int comp_start = 0, comp_end = 0;

    do {
        switch(path[comp_end]) {
            case '\0': [[fallthrough]];
            case '/':
                if(comp_start == comp_end) {
                    comp_start++;
                    break;
                }
                bool final = path[comp_end] == '\0';

                int comp_length = comp_end - comp_start;
                char* component = heap_alloc(comp_length + 1);
                memory_copy(component, path + comp_start, comp_length);
                component[comp_length] = 0;
                comp_start = comp_end + 1;

                vfs_dentry_t* next_dentry;
                vfs_result_t res = lookup_component(current, component, &next_dentry);
                heap_free(component, comp_length + 1);

                if(res != VFS_RESULT_OK) {
                    vfs_dentry_put(current);
                    return res;
                }

                vfs_dentry_put(current);
                current = next_dentry;

                res = traverse_mounts(&current);
                if(res != VFS_RESULT_OK) {
                    vfs_dentry_put(current);
                    return res;
                }

                if(current->node != nullptr && current->node->type == VFS_NODE_TYPE_LINK && (follow_final || !final)) {
                    char* target;
                    size_t target_size;
                    res = read_link_target(current->node, &target, &target_size);
                    if(res != VFS_RESULT_OK) {
                        vfs_dentry_put(current);
                        return res;
                    }

                    // The new path is the target followed by the remainder of the path,
                    size_t target_length = string_length(target);
                    size_t remaining_length = string_length(path + comp_end);
                    size_t new_length = target_length + remaining_length;
                    char* new_path = heap_alloc(new_length + 1);
                    memory_copy(new_path, target, target_length);
                    memory_copy(new_path + target_length, path + comp_end, remaining_length + 1);
                    heap_free(target, target_size);

                    vfs_dentry_t* base = new_path[0] == '/' ? root_dentry() : vfs_dentry_get(current->parent != nullptr ? current->parent : current);

                    vfs_dentry_put(current);

                    if(base == nullptr) {
                        heap_free(new_path, new_length + 1);
                        return VFS_RESULT_ERR_NOT_FOUND;
                    }

                    res = resolve_path(base, new_path, follow_final, depth + 1, out);
                    heap_free(new_path, new_length + 1);
                    return res;
                }
                break;
        }
    } while(path[comp_end++]);

    *out = current;
    return VFS_RESULT_OK;
}

vfs_result_t vfs_lookup_dentry(const vfs_path_t* path, bool follow_final, vfs_dentry_t** out_result_dentry) {
    vfs_dentry_t* base;
    if(path->node == nullptr || path->rel_path[0] == '/') {
        base = root_dentry();
    } else {
        base = dentry_for_node(path->node);
    }

    if(base == nullptr)
        return VFS_RESULT_ERR_NOT_FOUND;

    vfs_result_t res = traverse_mounts(&base);
    if(res != VFS_RESULT_OK) {
        vfs_dentry_put(base);
        return res;
    }

    return resolve_path(base, path->rel_path, follow_final, 0, out_result_dentry);
}

vfs_result_t vfs_lookup(const vfs_path_t* path, bool follow_symlinks, vfs_node_t** out_result_node) {
    vfs_dentry_t* dentry;
    vfs_result_t res = vfs_lookup_dentry(path, follow_symlinks, &dentry);
    if(res != VFS_RESULT_OK)
        return res;

    if(dentry->node == nullptr) {
        vfs_dentry_put(dentry);
        return VFS_RESULT_ERR_NOT_FOUND;
    }

    *out_result_node = vfs_node_get(dentry->node);
    vfs_dentry_put(dentry);
    return VFS_RESULT_OK;
}

vfs_result_t vfs_path_to(vfs_node_t* node, char** out_buf, size_t* out_size) {
    vfs_dentry_t* dentry = dentry_for_node(node);
    if(dentry == nullptr)
        return VFS_RESULT_ERR_NOT_FOUND;

    size_t name_bytes = 0;
    size_t depth = 0;
    for(vfs_dentry_t* d = dentry; d != nullptr; d = d->parent) {
        if(d->name_length > 0) {
            name_bytes += d->name_length;
            depth++;
        }
    }

    size_t path_size = name_bytes + depth + 1;
    if(path_size < 2)
        path_size = 2;
    char* buf = heap_alloc(path_size);

    size_t end = path_size - 1;
    buf[end] = '\0';

    size_t pos = end;
    for(vfs_dentry_t* d = dentry; d != nullptr && d->name_length > 0; d = d->parent) {
        pos -= d->name_length;
        memory_copy(buf + pos, d->name, d->name_length);
        buf[--pos] = '/';
    }
    if(pos == end) {
        buf[0] = '/';
    }

    vfs_dentry_put(dentry);
    *out_buf = buf;
    *out_size = path_size;
    return VFS_RESULT_OK;
}

vfs_result_t vfs_read_dir(vfs_path_t* path, size_t* offset, vfs_dentry_t** out_dentry) {
    vfs_node_t* node;
    vfs_result_t res = vfs_lookup(path, true, &node);
    if(res != VFS_RESULT_OK)
        return res;

    vfs_dentry_t* dir_dentry = dentry_for_node(node);
    if(dir_dentry == nullptr) {
        vfs_node_put(node);
        return VFS_RESULT_ERR_NOT_FOUND;
    }

    vfs_node_t* child_node = nullptr;
    const char* child_name = nullptr;

    lock_acquire(&node->lock);
    res = node->ops->read_dir(node, offset, &child_node, &child_name);
    lock_release(&node->lock);

    if(res != VFS_RESULT_OK) {
        vfs_dentry_put(dir_dentry);
        vfs_node_put(node);
        return res;
    }

    if(child_name == nullptr) {
        *out_dentry = nullptr;
        vfs_dentry_put(dir_dentry);
        vfs_node_put(node);
        return VFS_RESULT_OK;
    }

    rwspinlock_write_t* dir_lock = rwspinlock_lock_write(&dir_dentry->rwspinlock);

    vfs_dentry_t* child_dentry = vfs_dcache_lookup(dir_dentry, child_name);
    if(child_dentry == nullptr) {
        res = vfs_dentry_create(dir_dentry, child_name, child_node, false, &child_dentry);
        if(child_node)
            vfs_node_put(child_node);
        if(res == VFS_RESULT_OK) {
            vfs_dcache_insert(child_dentry);
            vfs_dentry_get(child_dentry);
        }
    } else {
        if(child_node)
            vfs_node_put(child_node);
        if(child_dentry->negative) {
            vfs_dentry_put(child_dentry);
            child_dentry = nullptr;
            res = VFS_RESULT_ERR_NOT_FOUND;
        }
    }

    rwspinlock_unlock_write(dir_lock);

    vfs_dentry_put(dir_dentry);
    vfs_node_put(node);
    *out_dentry = child_dentry;
    return res;
}
