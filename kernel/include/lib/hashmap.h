#pragma once
#include <lib/list.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct hashmap hashmap_t;
typedef struct hashmap_node hashmap_node_t;

struct hashmap_node {
    list_node_t node;

    /// @internal The bucket this node is linked into, maintained by the hashmap
    size_t bucket;
};

/**
 * @brief Computes the hash of a key
 * @param key An opaque pointer to the key to hash
 * @return The hash of the key
 */
typedef size_t (*hashmap_hash_fn)(const void* key);

/**
 * @brief Compares the key embedded in a node against a standalone key
 * @param node The node whose key is compared
 * @param key The standalone key to compare against
 * @return true if the keys are equal, false otherwise
 */
typedef bool (*hashmap_equal_fn)(const hashmap_node_t* node, const void* key);

struct hashmap {
    /// The bucket array handed to hashmap_init()
    list_t* buckets;
    /// The number of buckets, must be a power of two
    size_t bucket_count;
    /// The number of nodes currently linked into the map.
    size_t count;

    hashmap_hash_fn hash;
    hashmap_equal_fn equal;
};

/**
 * @brief Initializes a hashmap over a caller provided bucket array
 * @param map The map to initialize
 * @param buckets The array of `bucket_count` lists to use as buckets
 * @param bucket_count The number of buckets, must be a non zero power of two
 * @param hash Callback that computes the hash of a key
 * @param equal Callback that compares a node's key against a standalone key
 */
void hashmap_init(hashmap_t* map, list_t* buckets, size_t bucket_count, hashmap_hash_fn hash, hashmap_equal_fn equal);

/**
 * @brief Finds the node whose key matches `key`
 * @return The matching node, or nullptr if none is present
 */
hashmap_node_t* hashmap_find(const hashmap_t* map, const void* key);

/**
 * @brief Links a node into the map
 * @param node The node to insert. It must not already be linked.
 * @note Inserting a node with a key that is already present results in two
 * nodes sharing a key; use hashmap_find() first if uniqueness is required
 */
void hashmap_insert(hashmap_t* map, const void* key, hashmap_node_t* node);

/**
 * @brief Unlinks a node from the map
 * @return true if the node was linked and is now removed, false otherwise
 */
bool hashmap_remove(hashmap_t* map, hashmap_node_t* node);

/**
 * @brief Returns the first node in the map, or nullptr if the map is empty
 */
hashmap_node_t* hashmap_find_first(const hashmap_t* map);

/**
 * @brief Returns the node following `current`, or nullptr at the end
 * @param current The node previously returned by hashmap_find_first() or hashmap_find_next()
 * @note `current` must still be linked into the map
 */
hashmap_node_t* hashmap_find_next(const hashmap_t* map, const hashmap_node_t* current);

/**
 * @brief Returns the number of nodes currently in the map
 */
static inline size_t hashmap_count(const hashmap_t* map) {
    return map->count;
}
