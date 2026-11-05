#include <common/assert.h>
#include <lib/hashmap.h>

/// @brief Returns the index of the bucket a key hashes to.
static size_t hashmap_bucket(const hashmap_t* map, const void* key) {
    return map->hash(key) & (map->bucket_count - 1);
}

void hashmap_init(hashmap_t* map, list_t* buckets, size_t bucket_count, hashmap_hash_fn hash, hashmap_equal_fn equal) {
    assert(bucket_count != 0);
    assert((bucket_count & (bucket_count - 1)) == 0);

    map->buckets = buckets;
    map->bucket_count = bucket_count;
    map->count = 0;
    map->hash = hash;
    map->equal = equal;

    for(size_t i = 0; i < bucket_count; i++) map->buckets[i] = LIST_INIT;
}

hashmap_node_t* hashmap_find(const hashmap_t* map, const void* key) {
    list_t* bucket = &map->buckets[hashmap_bucket(map, key)];
    LIST_FOR_EACH(bucket, node) {
        hashmap_node_t* candidate = CONTAINER_OF(node, hashmap_node_t, node);
        if(map->equal(candidate, key))
            return candidate;
    }
    return nullptr;
}

void hashmap_insert(hashmap_t* map, const void* key, hashmap_node_t* node) {
    node->bucket = hashmap_bucket(map, key);
    list_push_front(&map->buckets[node->bucket], &node->node);
    map->count++;
}

bool hashmap_remove(hashmap_t* map, hashmap_node_t* node) {
    list_t* bucket = &map->buckets[node->bucket];
    LIST_FOR_EACH(bucket, list_node) {
        if(list_node != &node->node)
            continue;

        list_node_delete(bucket, list_node);
        map->count--;
        return true;
    }
    return false;
}

hashmap_node_t* hashmap_find_first(const hashmap_t* map) {
    for(size_t i = 0; i < map->bucket_count; i++) {
        if(map->buckets[i].head != nullptr)
            return CONTAINER_OF(map->buckets[i].head, hashmap_node_t, node);
    }
    return nullptr;
}

hashmap_node_t* hashmap_find_next(const hashmap_t* map, const hashmap_node_t* current) {
    if(current == nullptr)
        return nullptr;

    if(current->node.next != nullptr)
        return CONTAINER_OF(current->node.next, hashmap_node_t, node);

    for(size_t i = current->bucket + 1; i < map->bucket_count; i++) {
        if(map->buckets[i].head != nullptr)
            return CONTAINER_OF(map->buckets[i].head, hashmap_node_t, node);
    }
    return nullptr;
}
