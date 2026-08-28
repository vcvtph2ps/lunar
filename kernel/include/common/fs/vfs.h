#pragma once
#include <common/fs/io.h>
#include <common/sync/rwlock.h>
#include <common/sync/spinlock.h>
#include <lib/helpers.h>
#include <lib/list.h>

typedef enum : uint8_t {
    VFS_RESULT_OK = 0,
    VFS_RESULT_ERR_NOT_FOUND,
    VFS_RESULT_ERR_EXISTS,

    VFS_RESULT_ERR_NOT_DIR,
    VFS_RESULT_ERR_NOT_FILE,

    VFS_RESULT_ERR_READ_ONLY,

    VFS_RESULT_ERR_UNSUPPORTED
} vfs_result_t;

typedef enum : uint8_t {
    VFS_NODE_TYPE_DIR,
    VFS_NODE_TYPE_FILE
} vfs_node_type_t;

typedef struct vfs vfs_t;
typedef struct vfs_node vfs_node_t;
typedef struct vfs_ops vfs_ops_t;
typedef struct vfs_node_ops vfs_node_ops_t;
typedef struct vfs_dentry vfs_dentry_t;
typedef struct vfs_dentry_ops vfs_dentry_ops_t;

typedef uint8_t vfs_permissions_t;

#define VFS_PERM_READ (1 << 0)
#define VFS_PERM_WRITE (1 << 1)
#define VFS_PERM_EXECUTE (1 << 2)

typedef struct {
    /// The size of the node in bytes, or 0 for directories
    size_t size;

    /// The type of the node (file or directory)
    vfs_node_type_t type;

    struct {
        vfs_permissions_t user;
        vfs_permissions_t group;
        vfs_permissions_t other;
    } permissions;
} vfs_node_attr_t;

struct vfs_ops {
    /// Constant name of the vfs
    const char* name;

    /// @brief Mounts the VFS instance, called once per instance when mounted...
    vfs_result_t (*mount)(vfs_t* vfs);

    /// @brief Unmounts the VFS instance, called once per instance when unmounted...
    vfs_result_t (*unmount)(vfs_t* vfs);


    /// @brief Gets the root node of the filesystem represented by the instance
    vfs_result_t (*get_root_node)(vfs_t* vfs, vfs_node_t** out_root);

    /// optional dentry operations (hashing, comparing) for this filesystem
    const vfs_dentry_ops_t* dentry_ops;
};

struct vfs {
    /// The vfs_ops_t instance for this vfs
    const vfs_ops_t* ops;

    /// The dentry that this vfs is mounted at
    struct vfs_dentry* mount_point;

    /// The list node for the global list of vfs instances
    list_node_t global_list_node;

    /// The private data for this vfs, used by the filesystem implementation
    void* private_data;
};

struct vfs_node_ops {
    /// @brief Looks up a child node by name in the given node
    vfs_result_t (*lookup)(vfs_node_t* node, const char* name, vfs_node_t** out_node);

    /// @brief Reads the next entry in the directory represented by the given node, starting at the given offset
    vfs_result_t (*read_dir)(vfs_node_t* node, size_t* offset, vfs_node_t** out_node, const char** out_name);

    /// @brief Performs IO on the given node
    vfs_result_t (*perform_io)(vfs_node_t* node, io_request_t* request);

    // @brief Gets the attributes of the given node
    vfs_result_t (*get_attributes)(vfs_node_t* node, vfs_node_attr_t* out_attr);

    // @brief Called when the node's reference count drops to zero
    void (*release)(vfs_node_t* node);
};

struct vfs_node {
    rwlock_t lock;

    ATOMIC size_t refcount;

    /// The type of the node (file or directory)
    vfs_node_type_t type;

    /// The vfs instance that this node belongs to
    vfs_t* current_vfs;

    /// The operations for this node
    vfs_node_ops_t* ops;

    /// dentries aliased to this node, one per hardlink
    list_t dentries;

    /// The private data for this node, used by the filesystem implementation
    void* private_data;
};

typedef struct {
    /// The node to start the path from, or nullptr for the root node
    vfs_node_t* node;

    /// The relative path to the node from the starting node, or an absolute path if node is nullptr
    const char* rel_path;
} vfs_path_t;

#define VFS_MAKE_ABS_PATH(__path)               \
    (vfs_path_t) {                              \
        .node = (nullptr), .rel_path = (__path) \
    }

#define VFS_MAKE_REL_PATH(__node, __path)      \
    (vfs_path_t) {                             \
        .node = (__node), .rel_path = (__path) \
    }

#include <common/fs/dentry.h>

/**
 * @brief Create and mount a new vfs instance from ops at mount_point
 * @param ops The vfs_ops_t instance to use for the new vfs
 * @param mount_point The path to mount the vfs at
 * @param private_data A pointer to private data to pass to the vfs_ops_t instance
 */
vfs_result_t vfs_mount(const vfs_ops_t* ops, const vfs_path_t* mount_point, void* private_data);

/**
 * @brief Unmounts the vfs that is currently mounted at the specified path
 * @param path The path to unmount the vfs from
 */
vfs_result_t vfs_unmount(const vfs_path_t* path);

/**
 * @brief Retrieves the root node of the VFS
 * @param out_root_node A pointer to a vfs_node_t* that will be set to the root node of the VFS
 */
vfs_result_t vfs_root_node(vfs_node_t** out_root_node);

/**
 * @brief Looks up the dentry at the specified path and returns a pointer to it in out_result_dentry
 * @param path The path to look up the dentry at.
 * @param out_result_dentry A pointer to a vfs_dentry_t* that will be set to the dentry at the specified path if it exists
 */
vfs_result_t vfs_lookup_dentry(const vfs_path_t* path, vfs_dentry_t** out_result_dentry);

/**
 * @brief Looks up the node at the specified path and returns a pointer to it in out_result_node
 * @param path The path to look up the node at.
 * @param out_result_node A pointer to a vfs_node_t* that will be set to the node at the specified path if it exists
 */
vfs_result_t vfs_lookup(const vfs_path_t* path, vfs_node_t** out_result_node);

/**
 * @brief Performs IO on the specified node
 * @param path The path to find the node to perform the IO on
 * @param request The IO request to perform
 */
vfs_result_t vfs_perform_io(const vfs_path_t* path, io_request_t* request);

/**
 * @brief Gets the attributes of the given node
 * @param path The path to the node to get the attributes of
 * @param out_attr A pointer to a vfs_node_attr_t variable to receive the node attributes
 */
vfs_result_t vfs_get_attributes(const vfs_path_t* path, vfs_node_attr_t* out_attr);

/**
 * @brief Constructs a path representing the absolute path to the given node
 * @param node The vfs_node_t to construct the absolute path for
 * @param out_buf A pointer to a char* variable to receive the path. The caller is responsible for freeing this buffer using heap_free().
 * @param out_size A pointer to a size_t variable to receive the size of the path string in bytes, including the null terminator
 */
vfs_result_t vfs_path_to(vfs_node_t* node, char** out_buf, size_t* out_size);

/**
 * @brief Reads the next entry in the directory at the given path, starting at the given offset
 * @param path The path to the directory to read
 * @param offset A pointer to a size_t variable that will be updated to the next offset to read from.
 * @param out_dentry A pointer to a vfs_dentry_t* variable that will be set to the next entry in the directory.
 * @note The caller is responsible for releasing the reference to this dentry using vfs_dentry_put()
 */
vfs_result_t vfs_read_dir(vfs_path_t* path, size_t* offset, vfs_dentry_t** out_dentry);

/// @brief The global list of vfs instances
extern spinlock_t g_vfs_list_lock;
extern list_t g_vfs_list;

/// @brief The vfs_ops_t instance for the rdsk initramfs
extern const vfs_ops_t g_vfs_rdsk_ops;

/**
 * @brief Increment the reference count of a VFS node
 * @param node The node to reference
 * @return The same node
 */
vfs_node_t* vfs_node_get(vfs_node_t* node);

/**
 * @brief Decrement the reference count of a VFS node, freeing it if it reaches zero
 * @param node The node to dereference
 */
void vfs_node_put(vfs_node_t* node);
