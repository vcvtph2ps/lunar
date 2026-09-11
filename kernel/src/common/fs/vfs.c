#include <common/fs/dentry.h>
#include <common/fs/io.h>
#include <common/fs/vfs.h>
#include <common/sync/mutex.h>
#include <common/sync/rwlock.h>
#include <lib/string.h>
#include <memory/heap.h>

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

    rwlock_write_t* dir_lock = rwlock_lock_write(&current->rwlock);

    dentry = vfs_dcache_lookup(current, component);
    if(dentry != nullptr) {
        rwlock_unlock_write(dir_lock);
        if(dentry->negative) {
            vfs_dentry_put(dentry);
            return VFS_RESULT_ERR_NOT_FOUND;
        }
        *out_next = dentry;
        return VFS_RESULT_OK;
    }

    vfs_node_t* next_node;
    mutex_acquire(&current->node->lock);
    vfs_result_t res = current->node->ops->lookup(current->node, component, &next_node);
    mutex_release(&current->node->lock);

    if(res == VFS_RESULT_OK && next_node == nullptr) res = VFS_RESULT_ERR_NOT_FOUND;

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

    rwlock_unlock_write(dir_lock);
    return res;
}

static vfs_dentry_t* dentry_for_node(vfs_node_t* node) {
    mutex_acquire(&node->lock);
    vfs_dentry_t* dentry = nullptr;
    if(node->dentries.head != nullptr) {
        dentry = CONTAINER_OF(node->dentries.head, vfs_dentry_t, alias_node);
        vfs_dentry_get(dentry);
    }
    mutex_release(&node->lock);

    if(dentry != nullptr) return dentry;

    vfs_node_t* root_node;
    if(vfs_root_node(&root_node) != VFS_RESULT_OK) return nullptr;

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
    vfs_result_t res = vfs_lookup_dentry(mount_point, &dentry);
    if(res != VFS_RESULT_OK) {
        heap_free(vfs, sizeof(vfs_t));
        return res;
    }

    rwlock_write_t* write_lock = rwlock_lock_write(&dentry->rwlock);

    if(dentry->node == nullptr || dentry->node->type != VFS_NODE_TYPE_DIR) {
        heap_free(vfs, sizeof(vfs_t));
        rwlock_unlock_write(write_lock);
        vfs_dentry_put(dentry);
        return VFS_RESULT_ERR_NOT_DIR;
    }

    if(dentry->mounted_vfs != nullptr) {
        heap_free(vfs, sizeof(vfs_t));
        rwlock_unlock_write(write_lock);
        vfs_dentry_put(dentry);
        return VFS_RESULT_ERR_EXISTS;
    }

    dentry->mounted_vfs = vfs;
    vfs->mount_point = dentry;
    vfs->ops->mount(vfs);
    rwlock_unlock_write(write_lock);

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
        return VFS_RESULT_ERR_NOT_FOUND;
    }
    vfs_t* root_vfs = CONTAINER_OF(g_vfs_list.head, vfs_t, global_list_node);
    spinlock_unlock(&g_vfs_list_lock);

    vfs_node_t* root_node;
    vfs_result_t res = root_vfs->ops->get_root_node(root_vfs, &root_node);
    if(res != VFS_RESULT_OK) return res;

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
    vfs_result_t res = vfs_lookup(path, &node);
    if(res != VFS_RESULT_OK) return res;

    // @todo: read/write mutex
    mutex_acquire(&node->lock);
    res = node->ops->perform_io(node, request);
    mutex_release(&node->lock);

    vfs_node_put(node);
    return res;
}

vfs_result_t vfs_perform_io_node(vfs_node_t* node, io_request_t* request) {
    vfs_node_get(node);
    vfs_result_t res;

    // @todo: read/write mutex
    mutex_acquire(&node->lock);
    res = node->ops->perform_io(node, request);
    mutex_release(&node->lock);

    vfs_node_put(node);
    return res;
}

vfs_result_t vfs_get_attributes(const vfs_path_t* path, vfs_node_attr_t* attr) {
    vfs_node_t* node;
    vfs_result_t res = vfs_lookup(path, &node);
    if(res != VFS_RESULT_OK) return res;

    // @todo: read/write mutex
    mutex_acquire(&node->lock);
    res = node->ops->get_attributes(node, attr);
    mutex_release(&node->lock);

    vfs_node_put(node);
    return res;
}

vfs_result_t vfs_lookup_dentry(const vfs_path_t* path, vfs_dentry_t** out_result_dentry) {
    int comp_start = 0, comp_end = 0;

    vfs_dentry_t* current_dentry;
    if(path->node == nullptr || path->rel_path[0] == '/') {
        vfs_node_t* root_node;
        vfs_result_t res = vfs_root_node(&root_node);
        if(res != VFS_RESULT_OK) return res;
        current_dentry = dentry_for_node(root_node);
        vfs_node_put(root_node);
        if(current_dentry == nullptr) return VFS_RESULT_ERR_NOT_FOUND;
        if(path->rel_path[0] == '/') {
            comp_start++;
            comp_end++;
        }
    } else {
        current_dentry = dentry_for_node(path->node);
        if(current_dentry == nullptr) return VFS_RESULT_ERR_NOT_FOUND;
    }

    vfs_result_t res = VFS_RESULT_OK;
    do {
        switch(path->rel_path[comp_end]) {
            case '\0': [[fallthrough]];
            case '/':
                if(comp_start == comp_end) {
                    comp_start++;
                    break;
                }
                int comp_length = comp_end - comp_start;
                char* component = heap_alloc(comp_length + 1);
                memory_copy(component, path->rel_path + comp_start, comp_length);
                component[comp_length] = 0;
                comp_start = comp_end + 1;

                vfs_dentry_t* next_dentry;
                res = lookup_component(current_dentry, component, &next_dentry);
                heap_free(component, comp_length + 1);

                if(res != VFS_RESULT_OK) {
                    vfs_dentry_put(current_dentry);
                    return res;
                }

                vfs_dentry_put(current_dentry);
                current_dentry = next_dentry;
                break;
        }

        if(current_dentry->mounted_vfs == nullptr) continue;

        vfs_dentry_t* mount_root = vfs_dcache_lookup(current_dentry, "");
        if(mount_root != nullptr) {
            vfs_dentry_put(current_dentry);
            current_dentry = mount_root;
            continue;
        }

        vfs_node_t* mount_root_node;
        res = current_dentry->mounted_vfs->ops->get_root_node(current_dentry->mounted_vfs, &mount_root_node);
        if(res != VFS_RESULT_OK) {
            vfs_dentry_put(current_dentry);
            return res;
        }

        res = vfs_dentry_create(current_dentry, "", mount_root_node, false, &mount_root);
        vfs_node_put(mount_root_node);
        if(res != VFS_RESULT_OK) {
            vfs_dentry_put(current_dentry);
            return res;
        }
        vfs_dcache_insert(mount_root);
        vfs_dentry_get(mount_root);
        vfs_dentry_put(current_dentry);
        current_dentry = mount_root;
    } while(path->rel_path[comp_end++]);

    *out_result_dentry = current_dentry;
    return VFS_RESULT_OK;
}

vfs_result_t vfs_lookup(const vfs_path_t* path, vfs_node_t** out_result_node) {
    vfs_dentry_t* dentry;
    vfs_result_t res = vfs_lookup_dentry(path, &dentry);
    if(res != VFS_RESULT_OK) return res;

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
    if(dentry == nullptr) return VFS_RESULT_ERR_NOT_FOUND;

    size_t name_bytes = 0;
    size_t depth = 0;
    for(vfs_dentry_t* d = dentry; d != nullptr; d = d->parent) {
        if(d->name_length > 0) {
            name_bytes += d->name_length;
            depth++;
        }
    }

    size_t path_size = name_bytes + depth + 1;
    if(path_size < 2) path_size = 2;
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
    vfs_result_t res = vfs_lookup(path, &node);
    if(res != VFS_RESULT_OK) return res;

    vfs_dentry_t* dir_dentry = dentry_for_node(node);
    if(dir_dentry == nullptr) {
        vfs_node_put(node);
        return VFS_RESULT_ERR_NOT_FOUND;
    }

    vfs_node_t* child_node = nullptr;
    const char* child_name = nullptr;

    mutex_acquire(&node->lock);
    res = node->ops->read_dir(node, offset, &child_node, &child_name);
    mutex_release(&node->lock);

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

    rwlock_write_t* dir_lock = rwlock_lock_write(&dir_dentry->rwlock);

    vfs_dentry_t* child_dentry = vfs_dcache_lookup(dir_dentry, child_name);
    if(child_dentry == nullptr) {
        res = vfs_dentry_create(dir_dentry, child_name, child_node, false, &child_dentry);
        if(child_node) vfs_node_put(child_node);
        if(res == VFS_RESULT_OK) {
            vfs_dcache_insert(child_dentry);
            vfs_dentry_get(child_dentry);
        }
    } else {
        if(child_node) vfs_node_put(child_node);
        if(child_dentry->negative) {
            vfs_dentry_put(child_dentry);
            child_dentry = nullptr;
            res = VFS_RESULT_ERR_NOT_FOUND;
        }
    }

    rwlock_unlock_write(dir_lock);

    vfs_dentry_put(dir_dentry);
    vfs_node_put(node);
    *out_dentry = child_dentry;
    return res;
}
