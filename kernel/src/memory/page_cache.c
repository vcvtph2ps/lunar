#include <common/arch.h>
#include <common/log.h>
#include <lib/helpers.h>
#include <lib/string.h>
#include <memory/heap.h>
#include <memory/page_cache.h>
#include <memory/pagedb.h>
#include <memory/pmm.h>
#include <memory/ptm.h>
#include <memory/vm.h>

#define PAGE_CACHE_BUCKET_COUNT 256

static spinlock_no_dpc_t g_page_cache_lock = SPINLOCK_NO_DPC_INIT;
static list_t g_page_cache_buckets[PAGE_CACHE_BUCKET_COUNT];
static hashmap_t g_page_cache;
static list_t g_page_cache_lru = LIST_INIT;
ATOMIC static size_t g_page_cache_page_count = 0;

/// @brief The key a cached page is filed under.
typedef struct {
    vfs_page_cache_object_t* object;
    uint64_t index;
} page_cache_key_t;

static size_t page_cache_hash(const void* key);
static bool page_cache_equal(const hashmap_node_t* node, const void* key);

void page_cache_init(void) {
    hashmap_init(&g_page_cache, g_page_cache_buckets, PAGE_CACHE_BUCKET_COUNT, page_cache_hash, page_cache_equal);
    g_page_cache_lru = LIST_INIT;
    ATOMIC_STORE(&g_page_cache_page_count, 0, ATOMIC_RELAXED);
}

static size_t page_cache_hash(const void* key) {
    const page_cache_key_t* page_key = key;
    uint64_t hash = ((uint64_t) (uintptr_t) page_key->object >> 4) * 0x9E3779B97F4A7C15ull;
    hash ^= (page_key->index + 1) * 0xC2B2AE3D27D4EB4Full;
    hash ^= hash >> 29;
    return (size_t) hash;
}

static bool page_cache_equal(const hashmap_node_t* node, const void* key) {
    const page_cache_page_t* page = CONTAINER_OF(node, page_cache_page_t, hash_node);
    const page_cache_key_t* page_key = key;
    return page->object == page_key->object && page->index == page_key->index;
}

/// @safety Assumes the page cache lock is held.
static page_cache_page_t* page_cache_find_locked(vfs_page_cache_object_t* object, uint64_t index) {
    page_cache_key_t key = { .object = object, .index = index };

    hashmap_node_t* node = hashmap_find(&g_page_cache, &key);
    if(node == nullptr)
        return nullptr;

    return CONTAINER_OF(node, page_cache_page_t, hash_node);
}

/// @brief Number of page table mappings currently referencing a physical page.
static uint32_t page_cache_map_count(uintptr_t physical) {
    if(physical == 0)
        return 0;

    pagedb_page_t* page = pagedb_get_page(physical / ARCH_PAGE_SIZE_4K);
    if(page == nullptr)
        return 0;

    return ATOMIC_LOAD(&page->allocated.map_count, ATOMIC_RELAXED);
}

/// @safety Assumes the page cache lock is held.
static void page_cache_lru_remove_locked(page_cache_page_t* page) {
    if(!page->in_lru)
        return;

    list_node_delete(&g_page_cache_lru, &page->lru_node);
    page->in_lru = false;
}

/// @safety Assumes the page cache lock is held.
static void page_cache_lru_push_locked(page_cache_page_t* page) {
    if(page->in_lru)
        return;

    list_push_front(&g_page_cache_lru, &page->lru_node);
    page->in_lru = true;
}

vfs_page_cache_object_t* page_cache_object_create(void* context, page_cache_fill_fn fill, page_cache_writeback_fn writeback, size_t size) {
    vfs_page_cache_object_t* object = heap_alloc(sizeof(vfs_page_cache_object_t));
    memory_zero(object, sizeof(vfs_page_cache_object_t));
    object->context = context;
    object->fill = fill;
    object->writeback = writeback;
    object->size = size;
    return object;
}

/// @safety Assumes the page cache lock is held.
static void page_cache_writeback_page_locked(page_cache_page_t* page) {
    if(!ATOMIC_LOAD(&page->dirty, ATOMIC_ACQUIRE))
        return;

    vfs_page_cache_object_t* object = page->object;
    if(object->writeback != nullptr)
        object->writeback(object->context, page->index, PTM_TO_HHDM(page->physical));

    ATOMIC_STORE(&page->dirty, false, ATOMIC_RELEASE);
}

static inline void release_spare(page_cache_page_t* page, uintptr_t physical) {
    if(page != nullptr)
        heap_free(page, sizeof(page_cache_page_t));
    if(physical != 0)
        pmm_free_page(physical);
}

page_cache_page_t* page_cache_get(vfs_page_cache_object_t* object, uint64_t index) {
    if(object == nullptr || object->fill == nullptr)
        return nullptr;
    if(index > (SIZE_MAX - PAGE_SIZE_DEFAULT) / PAGE_SIZE_DEFAULT)
        return nullptr;
    if(index * PAGE_SIZE_DEFAULT >= object->size)
        return nullptr;

    page_cache_page_t* spare_page = nullptr;
    uintptr_t spare_physical = 0;

    for(;;) {
        spinlock_no_dpc_lock(&g_page_cache_lock);

        page_cache_page_t* page = page_cache_find_locked(object, index);
        if(page != nullptr) {
            page_cache_page_state_t state = ATOMIC_LOAD(&page->state, ATOMIC_ACQUIRE);
            if(state == PAGE_CACHE_PAGE_READY) {
                ATOMIC_LOAD_ADD(&page->pins, 1, ATOMIC_ACQ_REL);
                page_cache_lru_remove_locked(page);
                spinlock_no_dpc_unlock(&g_page_cache_lock);
                release_spare(spare_page, spare_physical);
                return page;
            }

            if(state == PAGE_CACHE_PAGE_FAILED) {
                spinlock_no_dpc_unlock(&g_page_cache_lock);
                release_spare(spare_page, spare_physical);
                return nullptr;
            }

            /// The page exists but is currently unreadable, wait for it to become ready
            spinlock_no_dpc_unlock(&g_page_cache_lock);
            arch_spin_hint();
            continue;
        }

        /// The page is absent, we allocate the backing storage outside the lock to avoid nesting allocator locks beneath the page cache lock
        if(spare_page == nullptr) {
            spinlock_no_dpc_unlock(&g_page_cache_lock);

            spare_page = heap_alloc(sizeof(page_cache_page_t));
            spare_physical = pmm_alloc_page(PMM_FLAG_NONE);
            if(spare_page == nullptr || spare_physical == 0) {
                release_spare(spare_page, spare_physical);
                return nullptr;
            }
            continue;
        }

        memory_zero(spare_page, sizeof(page_cache_page_t));
        spare_page->object = object;
        spare_page->index = index;
        spare_page->physical = spare_physical;
        spare_page->in_lru = false;
        ATOMIC_STORE(&spare_page->pins, 0, ATOMIC_RELAXED);
        ATOMIC_STORE(&spare_page->state, PAGE_CACHE_PAGE_LOADING, ATOMIC_RELEASE);

        page = spare_page;
        page_cache_key_t key = (page_cache_key_t) { .object = object, .index = index };
        hashmap_insert(&g_page_cache, &key, &page->hash_node);
        ATOMIC_LOAD_ADD(&g_page_cache_page_count, 1, ATOMIC_RELAXED);

        spare_page = nullptr;
        spare_physical = 0;
        spinlock_no_dpc_unlock(&g_page_cache_lock);

        bool success = object->fill(object->context, index, PTM_TO_HHDM(page->physical));

        spinlock_no_dpc_lock(&g_page_cache_lock);
        if(success) {
            ATOMIC_STORE(&page->state, PAGE_CACHE_PAGE_READY, ATOMIC_RELEASE);
            /// we must hold a reference on behalf of the caller
            ATOMIC_LOAD_ADD(&page->pins, 1, ATOMIC_ACQ_REL);
            page_cache_lru_remove_locked(page);
        } else {
            ATOMIC_STORE(&page->state, PAGE_CACHE_PAGE_FAILED, ATOMIC_RELEASE);
            hashmap_remove(&g_page_cache, &page->hash_node);
            page_cache_lru_remove_locked(page);
            ATOMIC_LOAD_SUB(&g_page_cache_page_count, 1, ATOMIC_RELAXED);
        }
        spinlock_no_dpc_unlock(&g_page_cache_lock);

        if(success)
            return page;

        pmm_free_page(page->physical);
        heap_free(page, sizeof(page_cache_page_t));
        return nullptr;
    }
}

void page_cache_put(page_cache_page_t* page) {
    if(page == nullptr)
        return;

    spinlock_no_dpc_lock(&g_page_cache_lock);
    uint32_t previous = ATOMIC_LOAD_SUB(&page->pins, 1, ATOMIC_ACQ_REL);
    assert(previous > 0);
    if(previous == 1 && ATOMIC_LOAD(&page->state, ATOMIC_ACQUIRE) == PAGE_CACHE_PAGE_READY) {
        page_cache_lru_push_locked(page);
    }
    spinlock_no_dpc_unlock(&g_page_cache_lock);
}

page_cache_page_t* page_cache_peek(vfs_page_cache_object_t* object, uint64_t index) {
    if(object == nullptr)
        return nullptr;

    spinlock_no_dpc_lock(&g_page_cache_lock);
    page_cache_page_t* page = page_cache_find_locked(object, index);
    spinlock_no_dpc_unlock(&g_page_cache_lock);
    return page;
}

void page_cache_mark_dirty(page_cache_page_t* page) {
    if(page == nullptr)
        return;

    spinlock_no_dpc_lock(&g_page_cache_lock);
    ATOMIC_STORE(&page->dirty, true, ATOMIC_RELEASE);
    spinlock_no_dpc_unlock(&g_page_cache_lock);
}

bool page_cache_write(vfs_page_cache_object_t* object, uint64_t index, const void* src, size_t offset, size_t count) {
    if(offset + count > PAGE_SIZE_DEFAULT)
        return false;

    page_cache_page_t* page = page_cache_get(object, index);
    if(page == nullptr)
        return false;

    memory_copy((uint8_t*) PTM_TO_HHDM(page->physical) + offset, src, count);

    spinlock_no_dpc_lock(&g_page_cache_lock);
    ATOMIC_STORE(&page->dirty, true, ATOMIC_RELEASE);
    spinlock_no_dpc_unlock(&g_page_cache_lock);

    page_cache_put(page);
    return true;
}

size_t page_cache_writeback(vfs_page_cache_object_t* object) {
    if(object == nullptr || object->writeback == nullptr)
        return 0;

    size_t written = 0;

    spinlock_no_dpc_lock(&g_page_cache_lock);
    for(hashmap_node_t* node = hashmap_find_first(&g_page_cache); node != nullptr; node = hashmap_find_next(&g_page_cache, node)) {
        page_cache_page_t* page = CONTAINER_OF(node, page_cache_page_t, hash_node);
        if(page->object != object)
            continue;
        if(ATOMIC_LOAD(&page->state, ATOMIC_ACQUIRE) != PAGE_CACHE_PAGE_READY)
            continue;
        if(!ATOMIC_LOAD(&page->dirty, ATOMIC_ACQUIRE))
            continue;

        /// A page that is still mapped writable could be modified again after we clear its dirty bit, so leave it for eviction instead.
        if(page_cache_map_count(page->physical) != 0)
            continue;

        page_cache_writeback_page_locked(page);
        written++;
    }
    spinlock_no_dpc_unlock(&g_page_cache_lock);

    return written;
}

size_t page_cache_reclaim(size_t target_pages) {
    /// How many pages we detach under the lock before dropping it to free them
    enum {
        PAGE_CACHE_RECLAIM_BATCH = 16
    };

    size_t evicted = 0;

    while(evicted < target_pages) {
        page_cache_page_t* batch[PAGE_CACHE_RECLAIM_BATCH];
        size_t batch_count = 0;

        spinlock_no_dpc_lock(&g_page_cache_lock);

        size_t budget = g_page_cache_lru.count;
        while(evicted + batch_count < target_pages && budget > 0 && g_page_cache_lru.count > 0 && batch_count < PAGE_CACHE_RECLAIM_BATCH) {
            budget--;

            list_node_t* node = list_pop_back(&g_page_cache_lru);
            page_cache_page_t* page = CONTAINER_OF(node, page_cache_page_t, lru_node);
            page->in_lru = false;

            bool resident = ATOMIC_LOAD(&page->state, ATOMIC_ACQUIRE) == PAGE_CACHE_PAGE_READY;
            bool unused = ATOMIC_LOAD(&page->pins, ATOMIC_RELAXED) == 0;
            bool unmapped = page_cache_map_count(page->physical) == 0;

            if(!resident || !unused || !unmapped) {
                page_cache_lru_push_locked(page);
                continue;
            }

            page_cache_writeback_page_locked(page);
            hashmap_remove(&g_page_cache, &page->hash_node);
            ATOMIC_LOAD_SUB(&g_page_cache_page_count, 1, ATOMIC_RELAXED);
            batch[batch_count++] = page;
        }

        spinlock_no_dpc_unlock(&g_page_cache_lock);

        for(size_t i = 0; i < batch_count; i++) {
            pmm_free_page(batch[i]->physical);
            heap_free(batch[i], sizeof(page_cache_page_t));
        }

        evicted += batch_count;
        if(batch_count == 0)
            break;
    }

    return evicted;
}

bool page_cache_mark_unreadable(vfs_page_cache_object_t* object, uint64_t index) {
    if(object == nullptr)
        return false;

    spinlock_no_dpc_lock(&g_page_cache_lock);

    page_cache_page_t* page = page_cache_find_locked(object, index);
    if(page == nullptr) {
        spinlock_no_dpc_unlock(&g_page_cache_lock);
        return false;
    }

    bool resident = ATOMIC_LOAD(&page->state, ATOMIC_ACQUIRE) == PAGE_CACHE_PAGE_READY;
    bool unused = ATOMIC_LOAD(&page->pins, ATOMIC_RELAXED) == 0;
    bool unmapped = page_cache_map_count(page->physical) == 0;

    if(!resident || !unused || !unmapped) {
        spinlock_no_dpc_unlock(&g_page_cache_lock);
        return false;
    }

    page_cache_writeback_page_locked(page);
    hashmap_remove(&g_page_cache, &page->hash_node);
    page_cache_lru_remove_locked(page);
    ATOMIC_LOAD_SUB(&g_page_cache_page_count, 1, ATOMIC_RELAXED);

    spinlock_no_dpc_unlock(&g_page_cache_lock);

    pmm_free_page(page->physical);
    heap_free(page, sizeof(page_cache_page_t));
    return true;
}

void page_cache_object_destroy(vfs_page_cache_object_t* object) {
    if(object == nullptr)
        return;

    for(;;) {
        spinlock_no_dpc_lock(&g_page_cache_lock);

        page_cache_page_t* found = nullptr;
        for(hashmap_node_t* node = hashmap_find_first(&g_page_cache); node != nullptr; node = hashmap_find_next(&g_page_cache, node)) {
            page_cache_page_t* page = CONTAINER_OF(node, page_cache_page_t, hash_node);
            if(page->object != object)
                continue;

            bool unused = ATOMIC_LOAD(&page->pins, ATOMIC_RELAXED) == 0;
            bool unmapped = page_cache_map_count(page->physical) == 0;
            bool resident = ATOMIC_LOAD(&page->state, ATOMIC_ACQUIRE) == PAGE_CACHE_PAGE_READY;
            if(!unused || !unmapped || !resident)
                continue;

            page_cache_writeback_page_locked(page);
            hashmap_remove(&g_page_cache, &page->hash_node);
            page_cache_lru_remove_locked(page);
            ATOMIC_LOAD_SUB(&g_page_cache_page_count, 1, ATOMIC_RELAXED);
            found = page;
            break;
        }

        spinlock_no_dpc_unlock(&g_page_cache_lock);

        if(found == nullptr)
            break;

        pmm_free_page(found->physical);
        heap_free(found, sizeof(page_cache_page_t));
    }

    heap_free(object, sizeof(vfs_page_cache_object_t));
}

size_t page_cache_count(void) {
    return ATOMIC_LOAD(&g_page_cache_page_count, ATOMIC_RELAXED);
}
