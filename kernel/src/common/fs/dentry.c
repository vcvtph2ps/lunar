#include <common/fs/dentry.h>
#include <common/fs/vfs.h>
#include <common/sync/lock.h>
#include <common/sync/rwspinlock.h>
#include <lib/hashmap.h>
#include <lib/helpers.h>
#include <lib/string.h>
#include <memory/heap.h>

#define VFS_DCACHE_BUCKET_COUNT 64
#define VFS_DCACHE_MAX_ENTRIES 256

/// @brief The key a dentry is filed under.
typedef struct {
    const vfs_dentry_t* parent;
    const char* name;
} dcache_key_t;

static size_t dcache_hash(const void* key);
static bool dcache_equal(const hashmap_node_t* node, const void* key);

static list_t g_dcache_buckets[VFS_DCACHE_BUCKET_COUNT];

static struct {
    spinlock_t lock;
    hashmap_t map;
    list_t lru;
} g_dcache = {
    .map = {
        .buckets = g_dcache_buckets,
        .bucket_count = VFS_DCACHE_BUCKET_COUNT,
        .count = 0,
        .hash = dcache_hash,
        .equal = dcache_equal,
    },
};

/// @brief The dentry ops to use, or nullptr unless the fs supplies both hash and compare
static const vfs_dentry_ops_t* dcache_ops(const vfs_dentry_t* parent) {
    const vfs_dentry_ops_t* ops = parent != nullptr ? parent->ops : nullptr;
    if(ops != nullptr && ops->hash != nullptr && ops->compare != nullptr)
        return ops;
    return nullptr;
}

static size_t dcache_hash(const void* key) {
    const dcache_key_t* dentry_key = key;
    const vfs_dentry_ops_t* ops = dcache_ops(dentry_key->parent);

    uint64_t hash = ((uint64_t) (uintptr_t) dentry_key->parent >> 4) * 0x9E3779B97F4A7C15ull;
    if(ops != nullptr) {
        hash ^= (uint64_t) ops->hash(dentry_key->name) * 0xC2B2AE3D27D4EB4Full;
    } else {
        for(const unsigned char* c = (const unsigned char*) dentry_key->name; *c != '\0'; c++) hash = (hash ^ *c) * 0xC2B2AE3D27D4EB4Full;
    }

    hash ^= hash >> 29;
    return (size_t) hash;
}

static bool dcache_equal(const hashmap_node_t* node, const void* key) {
    const vfs_dentry_t* dentry = CONTAINER_OF(node, vfs_dentry_t, hash_node);
    const dcache_key_t* dentry_key = key;

    if(dentry->parent != dentry_key->parent)
        return false;

    const vfs_dentry_ops_t* ops = dcache_ops(dentry->parent);
    if(ops != nullptr)
        return ops->compare(dentry->name, dentry_key->name) == 0;

    return string_compare(dentry->name, dentry_key->name) == 0;
}

void vfs_dcache_insert(vfs_dentry_t* dentry) {
    vfs_dentry_t* evicted = nullptr;

    spinlock_lock(&g_dcache.lock);

    dcache_key_t key = (dcache_key_t) { .parent = dentry->parent, .name = dentry->name };
    hashmap_insert(&g_dcache.map, &key, &dentry->hash_node);
    list_push_back(&g_dcache.lru, &dentry->lru_node);

    for(size_t i = 0; g_dcache.map.count > VFS_DCACHE_MAX_ENTRIES && i < g_dcache.map.count; i++) {
        vfs_dentry_t* candidate = CONTAINER_OF(g_dcache.lru.head, vfs_dentry_t, lru_node);
        if(candidate->refcount > 1) {
            list_node_delete(&g_dcache.lru, &candidate->lru_node);
            list_push_back(&g_dcache.lru, &candidate->lru_node);
            continue;
        }

        hashmap_remove(&g_dcache.map, &candidate->hash_node);
        list_node_delete(&g_dcache.lru, &candidate->lru_node);

        evicted = candidate;
        break;
    }
    spinlock_unlock(&g_dcache.lock);

    if(evicted != nullptr)
        vfs_dentry_put(evicted);
}

vfs_dentry_t* vfs_dcache_lookup(vfs_dentry_t* parent, const char* name) {
    dcache_key_t key = { .parent = parent, .name = name };

    spinlock_lock(&g_dcache.lock);

    hashmap_node_t* node = hashmap_find(&g_dcache.map, &key);
    if(node == nullptr) {
        spinlock_unlock(&g_dcache.lock);
        return nullptr;
    }

    vfs_dentry_t* dentry = CONTAINER_OF(node, vfs_dentry_t, hash_node);
    dentry->refcount++;

    list_node_delete(&g_dcache.lru, &dentry->lru_node);
    list_push_back(&g_dcache.lru, &dentry->lru_node);

    spinlock_unlock(&g_dcache.lock);
    return dentry;
}

vfs_dentry_t* vfs_dentry_get(vfs_dentry_t* dentry) {
    spinlock_lock(&g_dcache.lock);
    dentry->refcount++;
    spinlock_unlock(&g_dcache.lock);
    return dentry;
}

void vfs_dentry_put(vfs_dentry_t* dentry) {
    while(dentry != nullptr) {
        spinlock_lock(&g_dcache.lock);
        assert(dentry->refcount > 0);
        size_t remaining = --dentry->refcount;
        spinlock_unlock(&g_dcache.lock);

        if(remaining > 0)
            return;

        if(dentry->node != nullptr) {
            lock_acquire(&dentry->node->lock);
            list_node_delete(&dentry->node->dentries, &dentry->alias_node);
            lock_release(&dentry->node->lock);
            vfs_node_put(dentry->node);
        }

        vfs_dentry_t* parent = dentry->parent;
        if(parent != nullptr) {
            spinlock_lock(&g_dcache.lock);
            list_node_delete(&parent->children, &dentry->sibling_node);
            spinlock_unlock(&g_dcache.lock);
        }
        heap_free((char*) dentry->name, dentry->name_length + 1);
        heap_free(dentry, sizeof(vfs_dentry_t));
        dentry = parent;
    }
}

vfs_result_t vfs_dentry_create(vfs_dentry_t* parent, const char* name, vfs_node_t* node, bool negative, vfs_dentry_t** out_dentry) {
    vfs_dentry_t* dentry = heap_zalloc(sizeof(vfs_dentry_t));
    rwspinlock_init(&dentry->rwspinlock);

    size_t name_length = string_length(name);
    dentry->name = heap_alloc(name_length + 1);
    memory_copy((char*) dentry->name, name, name_length + 1);
    dentry->name_length = name_length;

    dentry->node = node;
    dentry->negative = negative;
    ATOMIC_STORE(&dentry->refcount, 1, ATOMIC_RELAXED);

    if(node != nullptr && node->current_vfs != nullptr) {
        dentry->ops = node->current_vfs->ops->dentry_ops;
    } else if(parent != nullptr) {
        dentry->ops = parent->ops;
    }

    if(parent != nullptr) {
        dentry->parent = parent;
        vfs_dentry_get(parent);

        spinlock_lock(&g_dcache.lock);
        list_push_back(&parent->children, &dentry->sibling_node);
        spinlock_unlock(&g_dcache.lock);
    }

    if(node != nullptr) {
        vfs_node_get(node);
        lock_acquire(&node->lock);
        list_push_back(&node->dentries, &dentry->alias_node);
        lock_release(&node->lock);
    }

    *out_dentry = dentry;
    return VFS_RESULT_OK;
}
