#include <common/fs/vfs.h>
#include <lib/string.h>
#include <memory/heap.h>

#define INFO(VFS) ((info_t*) (VFS)->private_data)
#define FILE(NODE) ((rdsk_file_t*) (NODE)->private_data)
#define DIR(NODE) ((rdsk_dir_t*) (NODE)->private_data)

typedef uint64_t rdsk_index_t;


typedef struct [[gnu::packed]] {
    uint16_t entry_size;
    uint64_t entry_count;
    uint64_t offset;
} rdsk_table_t;

typedef struct [[gnu::packed]] {
    char signature[4];
    uint16_t revision;
    uint16_t header_size;
    uint64_t root_index;

    uint64_t nametable_offset;
    uint64_t nametable_size;

    rdsk_table_t dir_table;
    rdsk_table_t file_table;
} rdsk_header_t;

typedef struct [[gnu::packed]] {
    bool used;
    uint64_t nametable_offset;
    uint64_t data_offset;
    uint64_t size;
    uint64_t next_index;
    uint64_t parent_index;
} rdsk_file_t;

typedef struct [[gnu::packed]] {
    bool used;
    uint64_t nametable_offset;
    uint64_t filetable_index;
    uint64_t dirtable_index;
    uint64_t next_index;
    uint64_t parent_index;
} rdsk_dir_t;

typedef struct {
    rdsk_header_t* header;
    vfs_node_t** dir_cache;
    uint64_t dir_cache_size;
    vfs_node_t** file_cache;
    uint64_t file_cache_size;
} info_t;

#define MAKE_RDSK_VERSION(major, minor) ((major << 8) | minor)

static vfs_node_ops_t g_node_ops;

static const char* get_name(vfs_t* vfs, uint64_t offset) {
    info_t* info = (info_t*) vfs->private_data;
    return (const char*) ((uintptr_t) info->header + info->header->nametable_offset + offset);
}

static rdsk_dir_t* get_dir(vfs_t* vfs, rdsk_index_t index) {
    info_t* info = (info_t*) vfs->private_data;
    return (rdsk_dir_t*) ((uintptr_t) info->header + info->header->dir_table.offset + (index - 1) * info->header->dir_table.entry_size);
}

static rdsk_file_t* get_file(vfs_t* vfs, rdsk_index_t index) {
    info_t* info = (info_t*) vfs->private_data;
    return (rdsk_file_t*) ((uintptr_t) info->header + info->header->file_table.offset + (index - 1) * info->header->file_table.entry_size);
}

static rdsk_index_t get_dir_index(vfs_t* vfs, rdsk_dir_t* dir) {
    info_t* info = (info_t*) vfs->private_data;
    return ((uintptr_t) dir - ((uintptr_t) info->header + info->header->dir_table.offset)) / info->header->dir_table.entry_size + 1;
}

static rdsk_index_t get_file_index(vfs_t* vfs, rdsk_file_t* file) {
    info_t* info = (info_t*) vfs->private_data;
    return ((uintptr_t) file - ((uintptr_t) info->header + info->header->file_table.offset)) / info->header->file_table.entry_size + 1;
}

static vfs_node_t* create_vfs_dir_node(vfs_t* vfs, rdsk_index_t index) {
    info_t* info = INFO(vfs);
    if(info->dir_cache[index - 1]) return vfs_node_get(info->dir_cache[index - 1]);
    vfs_node_t* node = heap_alloc(sizeof(vfs_node_t));
    memory_set(node, 0, sizeof(vfs_node_t));
    rwlock_init(&node->lock);
    ATOMIC_STORE(&node->refcount, 1, ATOMIC_RELAXED);
    node->current_vfs = vfs;
    node->type = VFS_NODE_TYPE_DIR;
    rdsk_dir_t* dir = get_dir(vfs, index);
    node->private_data = dir;

    node->ops = &g_node_ops;
    info->dir_cache[index - 1] = node;
    return vfs_node_get(node);
}

static vfs_node_t* create_vfs_file_node(vfs_t* vfs, rdsk_index_t index) {
    info_t* info = INFO(vfs);
    if(info->file_cache[index - 1]) return vfs_node_get(info->file_cache[index - 1]);
    vfs_node_t* node = heap_alloc(sizeof(vfs_node_t));
    memory_set(node, 0, sizeof(vfs_node_t));
    rwlock_init(&node->lock);
    ATOMIC_STORE(&node->refcount, 1, ATOMIC_RELAXED);
    node->current_vfs = vfs;
    node->type = VFS_NODE_TYPE_FILE;
    node->private_data = get_file(vfs, index);
    node->ops = &g_node_ops;
    info->file_cache[index - 1] = node;
    return vfs_node_get(node);
}

static rdsk_dir_t* find_dir(vfs_t* vfs, rdsk_dir_t* dir, const char* name) {
    rdsk_index_t curindex = dir->dirtable_index;
    while(curindex != 0) {
        rdsk_dir_t* curdir = get_dir(vfs, curindex);
        curindex = curdir->next_index;
        if(string_compare(name, get_name(vfs, curdir->nametable_offset))) continue;
        return curdir;
    }
    return nullptr;
}

static rdsk_file_t* find_file(vfs_t* vfs, rdsk_dir_t* dir, const char* name) {
    rdsk_index_t curindex = dir->filetable_index;
    while(curindex != 0) {
        rdsk_file_t* curfile = get_file(vfs, curindex);
        curindex = curfile->next_index;
        if(string_compare(name, get_name(vfs, curfile->nametable_offset))) continue;
        return curfile;
    }
    return nullptr;
}

static vfs_result_t rdsk_node_attr(vfs_node_t* node, vfs_node_attr_t* out_attr) {
    out_attr->size = node->type == VFS_NODE_TYPE_FILE ? FILE(node)->size : 0;
    out_attr->type = node->type;

    out_attr->permissions.user = VFS_PERM_READ | VFS_PERM_EXECUTE;
    out_attr->permissions.group = VFS_PERM_READ | VFS_PERM_EXECUTE;
    out_attr->permissions.other = VFS_PERM_READ | VFS_PERM_EXECUTE;
    return VFS_RESULT_OK;
}

static vfs_result_t rdsk_node_lookup(vfs_node_t* node, const char* name, vfs_node_t** out_node) {
    if(node->type != VFS_NODE_TYPE_DIR) return VFS_RESULT_ERR_NOT_DIR;

    if(string_compare(name, ".") == 0) {
        *out_node = vfs_node_get(node);
        return VFS_RESULT_OK;
    }

    rdsk_file_t* found_file = find_file(node->current_vfs, DIR(node), name);
    if(found_file != nullptr) {
        *out_node = create_vfs_file_node(node->current_vfs, get_file_index(node->current_vfs, found_file));
        return VFS_RESULT_OK;
    }

    rdsk_dir_t* found_dir = find_dir(node->current_vfs, DIR(node), name);
    if(found_dir != nullptr) {
        *out_node = create_vfs_dir_node(node->current_vfs, get_dir_index(node->current_vfs, found_dir));
        return VFS_RESULT_OK;
    }

    return VFS_RESULT_ERR_NOT_FOUND;
}

static vfs_result_t rdsk_node_readdir(vfs_node_t* node, size_t* offset, vfs_node_t** out_node, const char** out_name) {
    if(node->type != VFS_NODE_TYPE_DIR) return VFS_RESULT_ERR_NOT_DIR;

    int local_offset = *offset;
    rdsk_header_t* header = INFO(node->current_vfs)->header;
    if(local_offset < (int) header->file_table.entry_count) {
        rdsk_index_t index = DIR(node)->filetable_index;
        for(int i = 0; i < local_offset && index != 0; i++) index = get_file(node->current_vfs, index)->next_index;
        if(index == 0) {
            local_offset = header->file_table.entry_count;
        } else {
            *out_name = (char*) get_name(node->current_vfs, get_file(node->current_vfs, index)->nametable_offset);
            if(out_node) *out_node = create_vfs_file_node(node->current_vfs, index);
            *offset = local_offset + 1;
            return VFS_RESULT_OK;
        }
    }

    rdsk_index_t index = DIR(node)->dirtable_index;
    for(int i = 0; i < local_offset - (int) header->file_table.entry_count && index != 0; i++) index = get_dir(node->current_vfs, index)->next_index;
    if(index == 0) {
        if(out_name) *out_name = nullptr;
        if(out_node) *out_node = nullptr;
        return VFS_RESULT_OK;
    }

    if(out_name) *out_name = (char*) get_name(node->current_vfs, get_dir(node->current_vfs, index)->nametable_offset);
    if(out_node) *out_node = create_vfs_dir_node(node->current_vfs, index);
    *offset = local_offset + 1;
    return VFS_RESULT_OK;
}

static vfs_result_t rdsk_node_read(vfs_node_t* node, void* buffer, size_t size, size_t offset, size_t* read_count) {
    if(node->type != VFS_NODE_TYPE_FILE) return VFS_RESULT_ERR_NOT_FILE;

    if(buffer == nullptr || size == 0) {
        assert(buffer == nullptr && size == 0);
        assert(read_count != nullptr);
        *read_count = FILE(node)->size;
        return VFS_RESULT_OK;
    }

    if(offset >= FILE(node)->size) {
        if(read_count) *read_count = 0;
        return VFS_RESULT_OK;
    }
    size_t count = FILE(node)->size - offset;
    if(count > size) count = size;
    memory_copy(buffer, (void*) (((uintptr_t) INFO(node->current_vfs)->header + FILE(node)->data_offset) + offset), count);
    if(read_count) *read_count = count;
    return VFS_RESULT_OK;
}

static vfs_result_t rdsk_perform_io(vfs_node_t* node, io_request_t* request) {
    if(request->type == IO_REQUEST_READ) {
        return rdsk_node_read(node, request->read.buffer, request->read.count, request->read.offset, &request->read.bytes_read);
    } else if(request->type == IO_REQUEST_WRITE) {
        return VFS_RESULT_ERR_READ_ONLY;
    } else {
        return VFS_RESULT_ERR_UNSUPPORTED;
    }
}

static vfs_result_t rdsk_umount(vfs_t* vfs) {
    (void) vfs;
    return VFS_RESULT_ERR_UNSUPPORTED;
}

static vfs_result_t rdsk_mount(vfs_t* vfs) {
    rdsk_header_t* header = (rdsk_header_t*) vfs->private_data;
    if(header->revision != MAKE_RDSK_VERSION(1, 1)) { return VFS_RESULT_ERR_UNSUPPORTED; }

    info_t* info = heap_alloc(sizeof(info_t));
    info->header = header;
    info->dir_cache_size = header->dir_table.entry_count;
    info->file_cache_size = header->file_table.entry_count;

    info->dir_cache = heap_alloc(sizeof(vfs_node_t*) * info->dir_cache_size);
    info->file_cache = heap_alloc(sizeof(vfs_node_t*) * info->file_cache_size);

    memory_set(info->dir_cache, 0, sizeof(vfs_node_t*) * info->dir_cache_size);
    memory_set(info->file_cache, 0, sizeof(vfs_node_t*) * info->file_cache_size);

    vfs->private_data = info;
    return VFS_RESULT_OK;
}

static vfs_result_t rdsk_root(vfs_t* vfs, vfs_node_t** root_node) {
    *root_node = create_vfs_dir_node(vfs, INFO(vfs)->header->root_index);
    return VFS_RESULT_OK;
}

static vfs_node_ops_t g_node_ops = { .lookup = rdsk_node_lookup, .perform_io = rdsk_perform_io, .read_dir = rdsk_node_readdir, .get_attributes = rdsk_node_attr };

const vfs_ops_t g_vfs_rdsk_ops = { .name = "rdsk", .mount = rdsk_mount, .unmount = rdsk_umount, .get_root_node = rdsk_root };
