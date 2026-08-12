#pragma once

#include <common/fs/vfs.h>

struct vfs_dentry_ops {
    /// Computes a hash for a path component name
    size_t (*hash)(const char* name);

    /// Compares two path component names (returns 0 if they match)
    int (*compare)(const char* a, const char* b);
};

typedef struct vfs_dentry {
    rwlock_t rwlock;

    /// The dentry operations for this dentry
    const vfs_dentry_ops_t* ops;

    /// The name of the path component this dentry represents
    const char* name;
    size_t name_length;

    /// The list of child dentries for this dentry
    list_t children;

    /// The node this dentry resolves to (nullptr for a negative dentry)
    vfs_node_t* node;

    /// The parent dentry (nullptr for the root dentry)
    struct vfs_dentry* parent;

    /// The vfs instance that is mounted at this dentry (nullptr if no vfs is mounted)
    vfs_t* mounted_vfs;

    /// The reference count for this dentry
    size_t refcount;

    /// Whether this dentry is a cached lookup miss
    bool negative;

    /// The list node for the sibling list of the parent dentry
    list_node_t sibling_node;

    /// The list node for the alias list of the node this dentry resolves to
    list_node_t alias_node;

    /// The list node for the hash table of the dentry cache
    list_node_t hash_node;

    /// The list node for the LRU list of the dentry cache
    list_node_t lru_node;
} vfs_dentry_t; // NOLINT

/// @brief Retains a reference on the given dentry, keeping it alive.
vfs_dentry_t* vfs_dentry_get(vfs_dentry_t* dentry); // NOLINT

/// @brief Releases a reference on the given dentry, freeing it once all references are dropped.
void vfs_dentry_put(vfs_dentry_t* dentry); // NOLINT

/**
 * @brief Creates a new dentry, linking it into its parent's child list and its node's alias list.
 * @param parent The parent directory dentry (nullptr for the root dentry)
 * @param name The name of the entry; a copy is taken
 * @param node The node the dentry resolves to (nullptr for a negative dentry)
 * @param negative Whether this dentry is a cached lookup miss
 * @param out_dentry Receives the new dentry, with a reference owned by the caller
 */
vfs_result_t vfs_dentry_create(vfs_dentry_t* parent, const char* name, vfs_node_t* node, bool negative, vfs_dentry_t** out_dentry); // NOLINT

/**
 * @brief Looks up a dentry in the dentry cache.
 * @param parent The parent directory dentry
 * @param name The name of the entry to find
 * @return The cached dentry with a reference held by the caller, or nullptr on a cache miss
 */
vfs_dentry_t* vfs_dcache_lookup(vfs_dentry_t* parent, const char* name); // NOLINT

/**
 * @brief Inserts a dentry into the cache.
 * @param dentry The dentry to insert
 */
void vfs_dcache_insert(vfs_dentry_t* dentry); // NOLINT
