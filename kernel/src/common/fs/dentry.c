#include <common/fs/dentry.h>
#include <common/fs/vfs.h>
#include <common/sync/mutex.h>
#include <common/sync/rwlock.h>
#include <lib/helpers.h>
#include <lib/string.h>
#include <memory/heap.h>

#define VFS_DCACHE_BUCKET_COUNT 64
#define VFS_DCACHE_MAX_ENTRIES 256

static struct {
    spinlock_t lock;
    list_t buckets[VFS_DCACHE_BUCKET_COUNT];
    list_t lru;
    size_t count;
} g_dcache;

static size_t dcache_hash(const vfs_dentry_t* parent, const char* name) {
    size_t hash = 5381;

    if(parent != nullptr) {
        hash = ((hash << 5) + hash) ^ (size_t) parent;

        if(parent->ops != nullptr && parent->ops->hash != nullptr) {
            hash = ((hash << 5) + hash) ^ parent->ops->hash(name);
            return hash;
        }

        hash = dcache_hash(parent->parent, parent->name);
    }

    for(const unsigned char* c = (const unsigned char*) name; *c != '\0'; c++) {
        hash = ((hash << 5) + hash) ^ *c;
    }

    return hash;
}

void vfs_dcache_insert(vfs_dentry_t* dentry) {
    vfs_dentry_t* evicted = nullptr;

    spinlock_lock(&g_dcache.lock);
    list_t* bucket = &g_dcache.buckets[dcache_hash(dentry->parent, dentry->name) & (VFS_DCACHE_BUCKET_COUNT - 1)];
    list_push_back(bucket, &dentry->hash_node);
    list_push_back(&g_dcache.lru, &dentry->lru_node);
    g_dcache.count++;

    for(size_t i = 0; g_dcache.count > VFS_DCACHE_MAX_ENTRIES && i < g_dcache.count; i++) {
        vfs_dentry_t* candidate = CONTAINER_OF(g_dcache.lru.head, vfs_dentry_t, lru_node);
        if(candidate->refcount > 1) {
            list_node_delete(&g_dcache.lru, &candidate->lru_node);
            list_push_back(&g_dcache.lru, &candidate->lru_node);
            continue;
        }

        list_node_delete(&g_dcache.buckets[dcache_hash(candidate->parent, candidate->name) & (VFS_DCACHE_BUCKET_COUNT - 1)], &candidate->hash_node);
        list_node_delete(&g_dcache.lru, &candidate->lru_node);

        g_dcache.count--;
        evicted = candidate;
        break;
    }
    spinlock_unlock(&g_dcache.lock);

    if(evicted != nullptr) vfs_dentry_put(evicted);
}

vfs_dentry_t* vfs_dcache_lookup(vfs_dentry_t* parent, const char* name) {
    spinlock_lock(&g_dcache.lock);
    list_t* bucket = &g_dcache.buckets[dcache_hash(parent, name) & (VFS_DCACHE_BUCKET_COUNT - 1)];
    LIST_FOR_EACH(bucket, node) {
        vfs_dentry_t* dentry = CONTAINER_OF(node, vfs_dentry_t, hash_node);
        if(dentry->parent == parent) {
            int cmp;
            if(parent->ops != nullptr && parent->ops->compare != nullptr) {
                cmp = parent->ops->compare(dentry->name, name);
            } else {
                cmp = string_compare(dentry->name, name);
            }
            if(cmp == 0) {
                dentry->refcount++;

                list_node_delete(&g_dcache.lru, &dentry->lru_node);
                list_push_back(&g_dcache.lru, &dentry->lru_node);

                spinlock_unlock(&g_dcache.lock);
                return dentry;
            }
        }
    }
    spinlock_unlock(&g_dcache.lock);
    return nullptr;
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

        if(remaining > 0) return;

        if(dentry->node != nullptr) {
            mutex_acquire(&dentry->node->lock);
            list_node_delete(&dentry->node->dentries, &dentry->alias_node);
            mutex_release(&dentry->node->lock);
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
    rwlock_init(&dentry->rwlock);

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
        mutex_acquire(&node->lock);
        list_push_back(&node->dentries, &dentry->alias_node);
        mutex_release(&node->lock);
    }

    *out_dentry = dentry;
    return VFS_RESULT_OK;
}
