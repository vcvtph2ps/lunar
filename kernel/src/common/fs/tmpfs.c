#include <common/fs/tmpfs.h>
#include <common/fs/vfs_inode.h>
#include <common/log.h>
#include <common/sync/spinlock.h>
#include <lib/string.h>
#include <memory/heap.h>

#include "common/fs/vfs.h"

#define INFO(VFS) ((tmpfs_info_t*) (VFS)->private_data)
#define FILE(NODE) ((tmpfs_file_t*) (NODE)->private_data)

typedef struct tmpfs_file {
    uint64_t inode;

    spinlock_t lock;

    char* name;
    size_t name_length;

    uint8_t* content;
    size_t size;

    size_t capacity;

    vfs_inode_t* vfs_inode;

    vfs_node_permissions_t permissions;

    struct tmpfs_file* next;
} tmpfs_file_t;

static const vfs_node_permissions_t g_tmpfs_default_permissions = {
    .user = VFS_PERM_READ | VFS_PERM_WRITE,
    .group = VFS_PERM_READ | VFS_PERM_WRITE,
    .other = VFS_PERM_READ | VFS_PERM_WRITE,
};

typedef struct {
    spinlock_t lock;
    tmpfs_file_t* files;
    uint64_t root_inode;
    uint64_t next_inode;
} tmpfs_info_t;

static vfs_node_ops_t g_tmpfs_dir_ops;
static vfs_node_ops_t g_tmpfs_file_ops;

static bool tmpfs_read_page(vfs_inode_t* inode, uint64_t index, void* dest) {
    tmpfs_file_t* file = inode->private_data;

    spinlock_lock(&file->lock);

    size_t offset = index * PAGE_SIZE_DEFAULT;
    if(offset < file->size) {
        size_t count = file->size - offset;
        if(count > PAGE_SIZE_DEFAULT)
            count = PAGE_SIZE_DEFAULT;

        memory_copy(dest, file->content + offset, count);
    }

    spinlock_unlock(&file->lock);
    return true;
}

static bool tmpfs_write_page(vfs_inode_t* inode, uint64_t index, const void* src) {
    tmpfs_file_t* file = inode->private_data;

    spinlock_lock(&file->lock);

    size_t offset = index * PAGE_SIZE_DEFAULT;
    if(offset < file->size) {
        size_t count = file->size - offset;
        if(count > PAGE_SIZE_DEFAULT)
            count = PAGE_SIZE_DEFAULT;

        memory_copy(file->content + offset, src, count);
    }

    spinlock_unlock(&file->lock);
    return true;
}

/// @brief Grows (or shrinks) the file's backing store, data between the old and new size is zeroed
static bool tmpfs_resize(vfs_inode_t* inode, size_t new_size) {
    tmpfs_file_t* file = inode->private_data;

    spinlock_lock(&file->lock);

    bool success = true;
    size_t new_capacity = new_size == 0 ? 1 : new_size;
    if(new_capacity > file->capacity) {
        uint8_t* content = heap_realloc(file->content, file->capacity, new_capacity);
        if(content == nullptr) {
            success = false;
        } else {
            file->content = content;
            file->capacity = new_capacity;
        }
    }

    if(success) {
        if(new_size > file->size)
            memory_zero(file->content + file->size, new_size - file->size);

        file->size = new_size;
    }

    spinlock_unlock(&file->lock);
    return success;
}

static const vfs_inode_ops_t g_tmpfs_file_inode_ops = {
    .read_page = tmpfs_read_page,
    .write_page = tmpfs_write_page,
    .resize = tmpfs_resize,
};

static void tmpfs_free_file(tmpfs_file_t* file) {
    if(file == nullptr)
        return;

    /// Dropping the inode writes back any dirty pages, so the backing store must outlive it.
    if(file->vfs_inode != nullptr)
        vfs_inode_put(file->vfs_inode);

    if(file->content != nullptr)
        heap_free(file->content, file->capacity);
    if(file->name != nullptr)
        heap_free(file->name, file->name_length + 1);
    heap_free(file, sizeof(tmpfs_file_t));
}

/// @brief Allocates a new file, copies `size` bytes of `data` into it and links it into the directory.
static tmpfs_file_t* tmpfs_file_create(tmpfs_info_t* info, const char* name, const void* data, size_t size, vfs_node_permissions_t permissions) {
    tmpfs_file_t* file = heap_alloc(sizeof(tmpfs_file_t));
    memory_zero(file, sizeof(tmpfs_file_t));

    file->name_length = (size_t) string_length(name);
    file->name = heap_alloc(file->name_length + 1);
    memory_copy(file->name, name, file->name_length + 1);

    file->size = size;
    file->capacity = size == 0 ? 1 : size;
    file->content = heap_alloc(file->capacity);
    if(size != 0 && data != nullptr)
        memory_copy(file->content, data, size);

    file->permissions = permissions;
    file->vfs_inode = vfs_inode_create(&g_tmpfs_file_inode_ops, file, size);

    spinlock_lock(&info->lock);
    file->inode = info->next_inode++;
    if(info->files == nullptr) {
        info->files = file;
    } else {
        tmpfs_file_t* tail = info->files;
        while(tail->next != nullptr) tail = tail->next;
        tail->next = file;
    }
    spinlock_unlock(&info->lock);

    return file;
}

vfs_result_t tmpfs_add_file(vfs_t* vfs, const char* name, const void* data, size_t size) {
    tmpfs_info_t* info = INFO(vfs);
    if(info == nullptr)
        return VFS_RESULT_ERR_UNSUPPORTED;

    tmpfs_file_create(info, name, data, size, g_tmpfs_default_permissions);
    return VFS_RESULT_OK;
}

static tmpfs_file_t* tmpfs_find_file(tmpfs_info_t* info, const char* name) {
    spinlock_lock(&info->lock);
    for(tmpfs_file_t* file = info->files; file != nullptr; file = file->next) {
        if(string_compare(file->name, name) == 0) {
            spinlock_unlock(&info->lock);
            return file;
        }
    }
    spinlock_unlock(&info->lock);
    return nullptr;
}

static vfs_node_t* tmpfs_create_file_node(vfs_t* vfs, tmpfs_file_t* file) {
    vfs_node_t* node = heap_alloc(sizeof(vfs_node_t));
    memory_zero(node, sizeof(vfs_node_t));
    node->lock = LOCK_INIT;
    ATOMIC_STORE(&node->refcount, 1, ATOMIC_RELAXED);
    node->type = VFS_NODE_TYPE_FILE;
    node->current_vfs = vfs;
    node->ops = &g_tmpfs_file_ops;
    node->private_data = file;
    node->inode = vfs_inode_get(file->vfs_inode);
    return node;
}

static vfs_result_t tmpfs_dir_lookup(vfs_node_t* node, const char* name, vfs_node_t** out_node) {
    tmpfs_info_t* info = node->private_data;

    if(string_compare(name, ".") == 0) {
        *out_node = vfs_node_get(node);
        return VFS_RESULT_OK;
    }

    tmpfs_file_t* file = tmpfs_find_file(info, name);
    if(file == nullptr)
        return VFS_RESULT_ERR_NOT_FOUND;

    *out_node = tmpfs_create_file_node(node->current_vfs, file);
    return VFS_RESULT_OK;
}

static vfs_result_t tmpfs_dir_read_dir(vfs_node_t* node, size_t* offset, vfs_node_t** out_node, const char** out_name) {
    tmpfs_info_t* info = node->private_data;

    spinlock_lock(&info->lock);
    size_t index = 0;
    tmpfs_file_t* file = info->files;
    while(file != nullptr && index < *offset) {
        file = file->next;
        index++;
    }
    spinlock_unlock(&info->lock);

    if(file == nullptr) {
        if(out_name)
            *out_name = nullptr;
        if(out_node)
            *out_node = nullptr;
        return VFS_RESULT_OK;
    }

    if(out_name)
        *out_name = file->name;
    if(out_node)
        *out_node = tmpfs_create_file_node(node->current_vfs, file);
    *offset = index + 1;
    return VFS_RESULT_OK;
}

static vfs_result_t tmpfs_dir_create(vfs_node_t* node, const char* name, vfs_node_type_t type, vfs_node_permissions_t permissions, vfs_node_t** out_node) {
    tmpfs_info_t* info = node->private_data;

    if(type != VFS_NODE_TYPE_FILE)
        return VFS_RESULT_ERR_UNSUPPORTED;

    if(tmpfs_find_file(info, name) != nullptr)
        return VFS_RESULT_ERR_EXISTS;

    tmpfs_file_t* file = tmpfs_file_create(info, name, nullptr, 0, permissions);
    if(file == nullptr)
        return VFS_RESULT_ERR_NO_SPACE;

    if(out_node != nullptr)
        *out_node = tmpfs_create_file_node(node->current_vfs, file);

    return VFS_RESULT_OK;
}

static vfs_result_t tmpfs_dir_attr(vfs_node_t* node, vfs_node_attr_t* out_attr) {
    tmpfs_info_t* info = node->private_data;

    out_attr->size = 0;
    out_attr->type = VFS_NODE_TYPE_DIR;
    out_attr->inode = info->root_inode;
    out_attr->permissions.user = VFS_PERM_READ | VFS_PERM_EXECUTE;
    out_attr->permissions.group = VFS_PERM_READ | VFS_PERM_EXECUTE;
    out_attr->permissions.other = VFS_PERM_READ | VFS_PERM_EXECUTE;
    return VFS_RESULT_OK;
}

static vfs_result_t tmpfs_file_attr(vfs_node_t* node, vfs_node_attr_t* out_attr) {
    tmpfs_file_t* file = FILE(node);

    out_attr->size = file->size;
    out_attr->type = VFS_NODE_TYPE_FILE;
    out_attr->inode = file->inode;
    out_attr->permissions = file->permissions;
    return VFS_RESULT_OK;
}

static vfs_result_t tmpfs_mount(vfs_t* vfs) {
    tmpfs_mount_data_t* data = vfs->private_data;

    tmpfs_info_t* info = heap_alloc(sizeof(tmpfs_info_t));
    memory_zero(info, sizeof(tmpfs_info_t));
    info->lock = SPINLOCK_INIT;
    info->root_inode = 1;
    info->next_inode = 2;

    vfs->private_data = info;

    if(data != nullptr) {
        for(size_t i = 0; i < data->file_count; i++) {
            const tmpfs_mount_file_t* file = &data->files[i];
            tmpfs_add_file(vfs, file->name, file->data, file->size);
        }
    }

    return VFS_RESULT_OK;
}

static vfs_result_t tmpfs_unmount(vfs_t* vfs) {
    tmpfs_info_t* info = INFO(vfs);
    if(info == nullptr)
        return VFS_RESULT_ERR_UNSUPPORTED;

    tmpfs_file_t* file = info->files;
    while(file != nullptr) {
        tmpfs_file_t* next = file->next;
        tmpfs_free_file(file);
        file = next;
    }

    heap_free(info, sizeof(tmpfs_info_t));
    return VFS_RESULT_OK;
}

static vfs_result_t tmpfs_get_root_node(vfs_t* vfs, vfs_node_t** out_root) {
    vfs_node_t* node = heap_alloc(sizeof(vfs_node_t));
    memory_zero(node, sizeof(vfs_node_t));
    node->lock = LOCK_INIT;
    ATOMIC_STORE(&node->refcount, 1, ATOMIC_RELAXED);
    node->type = VFS_NODE_TYPE_DIR;
    node->current_vfs = vfs;
    node->ops = &g_tmpfs_dir_ops;
    node->private_data = INFO(vfs);

    *out_root = node;
    return VFS_RESULT_OK;
}

static vfs_node_ops_t g_tmpfs_dir_ops = {
    .lookup = tmpfs_dir_lookup,
    .read_dir = tmpfs_dir_read_dir,
    .create = tmpfs_dir_create,
    .get_attributes = tmpfs_dir_attr,
};

static vfs_node_ops_t g_tmpfs_file_ops = {
    .get_attributes = tmpfs_file_attr,
};

const vfs_ops_t g_vfs_tmpfs_ops = {
    .name = "tmpfs",
    .mount = tmpfs_mount,
    .unmount = tmpfs_unmount,
    .get_root_node = tmpfs_get_root_node,
};
