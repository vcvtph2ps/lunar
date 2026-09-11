#include <common/fs/devfs.h>
#include <common/fs/vfs.h>
#include <common/sync/mutex.h>
#include <common/sync/spinlock.h>
#include <lib/list.h>
#include <lib/string.h>
#include <memory/heap.h>

// @todo: rewrite this, currently it doesn't support directories at all, and just isn't a good way to do this...
// but it will work for now

typedef struct devfs_entry {
    char* name;
    size_t name_len;

    const devfs_device_ops_t* ops;
    void* ctx;

    list_node_t list_node;
} devfs_entry_t;

typedef struct {
    devfs_entry_t* entry;
} devfs_node_priv_t;


static spinlock_t g_devfs_lock;
static list_t g_devfs_entries;

static vfs_t* g_devfs_vfs = nullptr;

static vfs_node_t g_root_node;
static bool g_root_node_initialised = false;

static vfs_node_ops_t g_root_ops;
static vfs_node_ops_t g_device_ops;

static devfs_entry_t* find_entry_locked(const char* name) {
    LIST_FOR_EACH(&g_devfs_entries, node) {
        devfs_entry_t* entry = CONTAINER_OF(node, devfs_entry_t, list_node);
        if(string_compare(entry->name, name) == 0) {
            return entry;
        }
    }
    return nullptr;
}

static vfs_result_t root_lookup(vfs_node_t* node, const char* name, vfs_node_t** out_node) {
    (void) node;

    spinlock_lock(&g_devfs_lock);
    devfs_entry_t* entry = find_entry_locked(name);
    spinlock_unlock(&g_devfs_lock);

    if(entry == nullptr) {
        return VFS_RESULT_ERR_NOT_FOUND;
    }

    vfs_node_t* dev_node = heap_alloc(sizeof(vfs_node_t));
    memory_zero(dev_node, sizeof(vfs_node_t));
    dev_node->lock = MUTEX_INIT;
    ATOMIC_STORE(&dev_node->refcount, 1, ATOMIC_RELAXED);
    dev_node->type = VFS_NODE_TYPE_FILE;
    dev_node->current_vfs = g_devfs_vfs;
    dev_node->ops = &g_device_ops;

    devfs_node_priv_t* priv = heap_alloc(sizeof(devfs_node_priv_t));
    priv->entry = entry;
    dev_node->private_data = priv;

    *out_node = dev_node;
    return VFS_RESULT_OK;
}

static vfs_result_t root_read_dir(vfs_node_t* node, size_t* offset, vfs_node_t** out_node, const char** out_name) {
    (void) node;

    size_t idx = 0;

    spinlock_lock(&g_devfs_lock);

    LIST_FOR_EACH(&g_devfs_entries, list_node) {
        if(idx == *offset) {
            devfs_entry_t* entry = CONTAINER_OF(list_node, devfs_entry_t, list_node);

            *out_name = entry->name;

            if(out_node != nullptr) {
                spinlock_unlock(&g_devfs_lock);
                vfs_result_t res = root_lookup(node, entry->name, out_node);
                if(res != VFS_RESULT_OK) {
                    *out_name = nullptr;
                    return res;
                }
                *offset = idx + 1;
                return VFS_RESULT_OK;
            }

            spinlock_unlock(&g_devfs_lock);
            *offset = idx + 1;
            return VFS_RESULT_OK;
        }
        idx++;
    }

    spinlock_unlock(&g_devfs_lock);

    *out_name = nullptr;
    if(out_node) *out_node = nullptr;
    return VFS_RESULT_OK;
}

static vfs_result_t root_get_attributes(vfs_node_t* node, vfs_node_attr_t* out_attr) {
    (void) node;
    out_attr->size = 0;
    out_attr->type = VFS_NODE_TYPE_DIR;
    out_attr->permissions.user = VFS_PERM_READ | VFS_PERM_WRITE | VFS_PERM_EXECUTE;
    out_attr->permissions.group = VFS_PERM_READ | VFS_PERM_EXECUTE;
    out_attr->permissions.other = VFS_PERM_READ | VFS_PERM_EXECUTE;
    return VFS_RESULT_OK;
}

static vfs_node_ops_t g_root_ops = {
    .lookup = root_lookup,
    .read_dir = root_read_dir,
    .perform_io = nullptr,
    .get_attributes = root_get_attributes,
    .release = nullptr,
};

static vfs_result_t device_perform_io(vfs_node_t* node, io_request_t* request) {
    devfs_node_priv_t* priv = (devfs_node_priv_t*) node->private_data;
    if(priv->entry->ops == nullptr || priv->entry->ops->perform_io == nullptr) {
        return VFS_RESULT_ERR_UNSUPPORTED;
    }
    return priv->entry->ops->perform_io(priv->entry->ctx, request);
}

static vfs_result_t device_get_attributes(vfs_node_t* node, vfs_node_attr_t* out_attr) {
    devfs_node_priv_t* priv = (devfs_node_priv_t*) node->private_data;
    if(priv->entry->ops != nullptr && priv->entry->ops->get_attributes != nullptr) {
        return priv->entry->ops->get_attributes(priv->entry->ctx, out_attr);
    }

    out_attr->size = 0;
    out_attr->type = VFS_NODE_TYPE_FILE;
    out_attr->permissions.user = VFS_PERM_READ | VFS_PERM_WRITE;
    out_attr->permissions.group = VFS_PERM_READ | VFS_PERM_WRITE;
    out_attr->permissions.other = VFS_PERM_READ | VFS_PERM_WRITE;
    return VFS_RESULT_OK;
}

static void device_release(vfs_node_t* node) {
    devfs_node_priv_t* priv = (devfs_node_priv_t*) node->private_data;
    heap_free(priv, sizeof(devfs_node_priv_t));
    heap_free(node, sizeof(vfs_node_t));
}

static vfs_node_ops_t g_device_ops = {
    .lookup = nullptr,
    .read_dir = nullptr,
    .perform_io = device_perform_io,
    .get_attributes = device_get_attributes,
    .release = device_release,
};

static vfs_result_t devfs_vfs_mount(vfs_t* vfs) {
    g_devfs_vfs = vfs;

    if(!g_root_node_initialised) {
        memory_zero(&g_root_node, sizeof(vfs_node_t));
        g_root_node.lock = MUTEX_INIT;

        ATOMIC_STORE(&g_root_node.refcount, 1, ATOMIC_RELAXED);
        g_root_node.type = VFS_NODE_TYPE_DIR;
        g_root_node.current_vfs = vfs;
        g_root_node.ops = &g_root_ops;
        g_root_node.private_data = nullptr;
        g_root_node_initialised = true;
    } else {
        g_root_node.current_vfs = vfs;
    }

    return VFS_RESULT_OK;
}

static vfs_result_t devfs_vfs_unmount(vfs_t* vfs) {
    (void) vfs;
    return VFS_RESULT_ERR_UNSUPPORTED;
}

static vfs_result_t devfs_vfs_get_root_node(vfs_t* vfs, vfs_node_t** out_root) {
    (void) vfs;
    *out_root = vfs_node_get(&g_root_node);
    return VFS_RESULT_OK;
}

const vfs_ops_t g_vfs_devfs_ops = {
    .name = "devfs",
    .mount = devfs_vfs_mount,
    .unmount = devfs_vfs_unmount,
    .get_root_node = devfs_vfs_get_root_node,
    .dentry_ops = nullptr,
};

vfs_result_t devfs_bind(const char* name, const devfs_device_ops_t* ops, void* ctx) {
    spinlock_lock(&g_devfs_lock);

    if(find_entry_locked(name) != nullptr) {
        spinlock_unlock(&g_devfs_lock);
        return VFS_RESULT_ERR_EXISTS;
    }

    devfs_entry_t* entry = heap_alloc(sizeof(devfs_entry_t));
    entry->name_len = (size_t) string_length(name);
    entry->name = heap_alloc(entry->name_len + 1);
    memory_copy(entry->name, name, entry->name_len + 1);
    entry->ops = ops;
    entry->ctx = ctx;

    list_push_back(&g_devfs_entries, &entry->list_node);

    spinlock_unlock(&g_devfs_lock);
    return VFS_RESULT_OK;
}

vfs_result_t devfs_unbind(const char* name) {
    spinlock_lock(&g_devfs_lock);

    devfs_entry_t* entry = find_entry_locked(name);
    if(entry == nullptr) {
        spinlock_unlock(&g_devfs_lock);
        return VFS_RESULT_ERR_NOT_FOUND;
    }

    list_node_delete(&g_devfs_entries, &entry->list_node);
    spinlock_unlock(&g_devfs_lock);

    heap_free(entry->name, entry->name_len + 1);
    heap_free(entry, sizeof(devfs_entry_t));
    return VFS_RESULT_OK;
}
