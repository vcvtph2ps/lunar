#pragma once
#include <common/fs/io.h>
#include <common/fs/vfs.h>

/**
 * @brief Operations provided by a device driver registered with devfs
 */
typedef struct devfs_device_ops {
    /// @brief Performs IO on the device
    /// @param ctx The opaque context pointer supplied at bind time
    vfs_result_t (*perform_io)(void* ctx, io_request_t* request);

    /// @brief Returns the attributes of the device node (optional)
    /// @note When nullptr the devfs provides sensible defaults (size = 0, rw perms)
    /// @param ctx The opaque context pointer supplied at bind time
    vfs_result_t (*get_attributes)(void* ctx, vfs_node_attr_t* out_attr);
} devfs_device_ops_t;

/**
 * @brief Bind a device to devfs, making it accessible as /dev/<name>
 *
 * The devfs must already be mounted before calling this function The
 * @p name must be unique, a second bind with the same name returns
 * VFS_RESULT_ERR_EXISTS
 *
 * @param name The device entry name
 * @param ops Pointer to the device operation table (must remain valid)
 * @param ctx Opaque pointer forwarded to every ops callback
 * @return VFS_RESULT_OK on success
 */
vfs_result_t devfs_bind(const char* name, const devfs_device_ops_t* ops, void* ctx);

/**
 * @brief Unbind a device from devfs
 * @note any vnodes that are still open will function until their last reference is dropped, new lookups for the name will fail
 * @param name The device entry name to remove
 * @return VFS_RESULT_OK if the entry was found and removed, VFS_RESULT_ERR_NOT_FOUND if no such entry exists
 */
vfs_result_t devfs_unbind(const char* name);

/// @brief The VFS driver operations for devfs
extern const vfs_ops_t g_vfs_devfs_ops;
