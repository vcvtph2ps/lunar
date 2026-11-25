#include <common/assert.h>
#include <common/interrupts/dpc.h>
#include <common/sched/sched.h>
#include <common/sync/spinlock.h>
#include <lib/list.h>
#include <lib/string.h>
#include <memory/slab.h>
#include <memory/vm.h>

static spinlock_no_dpc_t g_slab_caches_lock = SPINLOCK_NO_DPC_INIT;
static list_t g_slab_caches = LIST_INIT;

static slab_cache_t g_alloc_cache;
static slab_cache_t g_alloc_magazine;

static inline size_t slab_cache_object_count(const slab_cache_t* cache) {
    return (cache->slab_size - sizeof(slab_t)) / cache->object_align;
}

static slab_t* slab_cache_create_slab(slab_cache_t* cache) {
    virt_addr_t block = (virt_addr_t) vm_map_anon_aligned(g_vm_global_address_space, VM_NO_HINT, cache->slab_size, cache->slab_align, VM_PROT_RW, VM_CACHE_NORMAL, VM_FLAG_NONE);
    if(block == 0) {
        return nullptr;
    }

    slab_t* slab = (slab_t*) ALIGN_DOWN(block + cache->slab_size - sizeof(slab_t), alignof(slab_t));
    slab->cache = cache;
    slab->buffer = block;
    slab->free_list = nullptr;
    slab->free_count = 0;

    size_t object_count = slab_cache_object_count(cache);
    assert(object_count > 0 && "slab must be able to hold at least one object");

    // Initialize the free list for the slab.
    for(size_t i = 0; i < object_count; i++) {
        void** obj = (void**) (block + i * cache->object_align);
        *obj = slab->free_list;
        slab->free_list = obj;
        slab->free_count++;
    }
    assert(slab->free_count == object_count);

    return slab;
}

static void slab_cache_destroy_slab(slab_cache_t* cache, slab_t* slab) {
    assert(slab != nullptr);
    assert(cache != nullptr);
    assert(slab->cache == cache);
    vm_unmap(g_vm_global_address_space, (void*) slab->buffer, cache->slab_size);
}

/// @safety: Assumes cache->slab_lock is held, **will** try to unlock it if required
static void* slab_cache_alloc_from_slab_lockless(slab_cache_t* cache) {
    if(cache->slab_partial_list.count == 0) {
        if(cache->slab_empty) {
            list_push(&cache->slab_partial_list, &cache->slab_empty->list_node);
            cache->slab_empty = nullptr;
        } else {
            spinlock_no_dpc_unlock(&cache->slab_lock);
            slab_t* new_slab = slab_cache_create_slab(cache);
            spinlock_no_dpc_lock(&cache->slab_lock);
            if(new_slab == nullptr) {
                return nullptr;
            }
            list_push(&cache->slab_partial_list, &new_slab->list_node);
        }
    }

    assert(cache->slab_partial_list.head);
    slab_t* slab = CONTAINER_OF(cache->slab_partial_list.head, slab_t, list_node);
    assert(slab->free_count > 0);

    void* ptr = slab->free_list;
    slab->free_list = *(void**) ptr;
    slab->free_count--;
    if(slab->free_count == 0) {
        list_node_delete(&cache->slab_partial_list, &slab->list_node);
        list_push(&cache->slab_full_list, &slab->list_node);
    }

    return ptr;
}

static void* slab_cache_alloc_from_slab(slab_cache_t* cache) {
    spinlock_no_dpc_lock(&cache->slab_lock);
    void* object = slab_cache_alloc_from_slab_lockless(cache);
    spinlock_no_dpc_unlock(&cache->slab_lock);
    return object;
}

static size_t slab_cache_alloc_many_from_slab(slab_cache_t* cache, void** objects, size_t object_count) {
    spinlock_no_dpc_lock(&cache->slab_lock);

    for(size_t i = 0; i < object_count; i++) {
        void* object = slab_cache_alloc_from_slab_lockless(cache);
        objects[i] = object;
        if(object == nullptr) {
            spinlock_no_dpc_unlock(&cache->slab_lock);
            return i;
        }
    }

    spinlock_no_dpc_unlock(&cache->slab_lock);
    return object_count;
}

/// @safety: Assumes cache->slab_lock is held
static void slab_cache_free_to_slab_lockless(slab_cache_t* cache, void* object) {
    slab_t* slab = (slab_t*) (ALIGN_DOWN((uintptr_t) object, cache->slab_align) + cache->slab_size - sizeof(slab_t));
    assert(slab->cache == cache);

    if(slab->free_count == 0) {
        list_node_delete(&cache->slab_full_list, &slab->list_node);
        list_push(&cache->slab_partial_list, &slab->list_node);
    }

    if(slab->free_count + 1 == slab_cache_object_count(cache)) {
        list_node_delete(&cache->slab_partial_list, &slab->list_node);

        if(cache->slab_empty) {
            slab_cache_destroy_slab(cache, cache->slab_empty);
        }
        cache->slab_empty = slab;
    }

    *(void**) object = slab->free_list;
    assert(object != nullptr);
    slab->free_list = object;
    slab->free_count++;
}

static void slab_cache_free_to_slab(slab_cache_t* cache, void* object) {
    spinlock_no_dpc_lock(&cache->slab_lock);
    slab_cache_free_to_slab_lockless(cache, object);
    spinlock_no_dpc_unlock(&cache->slab_lock);
}

static void slab_cache_free_many_to_slab(slab_cache_t* cache, void** objects, size_t object_count) {
    spinlock_no_dpc_lock(&cache->slab_lock);

    for(size_t i = 0; i < object_count; i++) {
        slab_cache_free_to_slab_lockless(cache, objects[i]);
    }

    spinlock_no_dpc_unlock(&cache->slab_lock);
}

static slab_magazine_t* slab_cache_create_magazine(slab_cache_t* cache, bool full) {
    slab_magazine_t* magazine = slab_cache_alloc_from_slab(&g_alloc_magazine);
    if(magazine == nullptr) {
        return nullptr;
    }

    magazine->rounds = full ? SLAB_MAGAZINE_SIZE : 0;
    for(size_t i = 0; i < SLAB_MAGAZINE_SIZE; i++) {
        if(full) {
            void* object = slab_cache_alloc_from_slab(cache);
            if(object == nullptr) {
                magazine->rounds = i;
                for(size_t j = i; j < SLAB_MAGAZINE_SIZE; j++) magazine->objects[j] = nullptr;
                return magazine;
            }
            magazine->objects[i] = object;
        } else {
            magazine->objects[i] = nullptr;
        }
    }

    return magazine;
}

static void slab_cache_destroy_magazine(slab_cache_t* cache, slab_magazine_t* magazine) {
    for(size_t i = 0; i < magazine->rounds; i++) {
        slab_cache_free_to_slab(cache, magazine->objects[i]);
    }
    slab_cache_free_to_slab(&g_alloc_magazine, magazine);
}

/// @safety: We call this function with dpcs and preemption already disabled
static inline void* slab_cpu_alloc(slab_cpu_cache_t* cc) {
    /// swap whenever secondary has anything in it to keep it empty on an allocation miss
    if(cc->primary->rounds == 0 && cc->secondary->rounds != 0) {
        slab_magazine_t* temp = cc->secondary;
        cc->secondary = cc->primary;
        cc->primary = temp;
    }

    if(cc->primary->rounds == 0) {
        return nullptr;
    }

    return cc->primary->objects[--cc->primary->rounds];
}

/// @safety: We call this function with dpcs and preemption already disabled
static inline bool slab_cpu_free(slab_cpu_cache_t* cc, void* ptr) {
    if(cc->primary->rounds < SLAB_MAGAZINE_SIZE) {
        cc->primary->objects[cc->primary->rounds++] = ptr;
        return true;
    }

    /// swap with a partially filled secondary so it ends up full on a free overflow
    if(cc->secondary->rounds != SLAB_MAGAZINE_SIZE) {
        slab_magazine_t* temp = cc->secondary;
        cc->secondary = cc->primary;
        cc->primary = temp;

        cc->primary->objects[cc->primary->rounds++] = ptr;
        return true;
    }

    return false;
}

slab_cache_t* slab_cache_create(const char* name, size_t object_size, size_t alignment) {
    assert(object_size >= sizeof(void*) && "object_size must be at least one pointer");
    assert(IS_POWER_OF_TWO(alignment) && "alignment must be a power of two");

    if(object_size > SIZE_MAX - (alignment - 1)) {
        return nullptr;
    }
    size_t object_align = ALIGN_UP(object_size, alignment);

    size_t slab_size = PAGE_SIZE_DEFAULT * 4;
    while(slab_size < alignment || slab_size - sizeof(slab_t) < object_align) {
        if(slab_size > SIZE_MAX / 2) {
            return nullptr;
        }
        slab_size <<= 1;
    }

    slab_cache_t* cache = slab_cache_alloc_from_slab(&g_alloc_cache);
    if(cache == nullptr) {
        return nullptr;
    }

    cache->name = name;
    cache->object_size = object_size;
    cache->object_align = object_align;
    cache->slab_size = slab_size;
    cache->slab_align = slab_size;

    /// Disable cpu caching for large objects
    cache->cpu_cached = (object_align <= PAGE_SIZE_DEFAULT) && (slab_cache_object_count(cache) >= SLAB_MAGAZINE_SIZE / 4);

    cache->slab_lock = SPINLOCK_NO_DPC_INIT;
    cache->slab_full_list = LIST_INIT;
    cache->slab_partial_list = LIST_INIT;
    cache->slab_empty = nullptr;

    cache->magazine_lock = SPINLOCK_NO_DPC_INIT;
    cache->magazine_full_list = LIST_INIT;
    cache->magazine_empty_list = LIST_INIT;

    memory_zero(cache->cpu_caches, g_init_boot_info->core_count * sizeof(cache->cpu_caches[0]));

    spinlock_no_dpc_lock(&g_slab_caches_lock);
    list_push(&g_slab_caches, &cache->list_node);
    spinlock_no_dpc_unlock(&g_slab_caches_lock);

    if(cache->cpu_cached) {
        for(size_t i = 0; i < SLAB_MAGAZINE_EXTRA; i++) {
            slab_magazine_t* magazine = slab_cache_create_magazine(cache, false);
            if(magazine == nullptr) {
                goto fail;
            }
            list_push(&cache->magazine_empty_list, &magazine->list_node);
        }

        for(size_t i = 0; i < g_init_boot_info->core_count; i++) {
            slab_magazine_t* magazine_primary = slab_cache_create_magazine(cache, true);
            cache->cpu_caches[i].primary = magazine_primary;
            if(magazine_primary == nullptr || magazine_primary->rounds == 0) {
                goto fail;
            }

            slab_magazine_t* magazine_secondary = slab_cache_create_magazine(cache, false);
            cache->cpu_caches[i].secondary = magazine_secondary;
            if(magazine_secondary == nullptr) {
                goto fail;
            }
        }
    }

    return cache;

fail:
    slab_cache_destroy(cache);
    return nullptr;
}

void slab_cache_destroy(slab_cache_t* cache) {
    if(cache == nullptr) {
        return;
    }

    spinlock_no_dpc_lock(&g_slab_caches_lock);
    list_node_delete(&g_slab_caches, &cache->list_node);
    spinlock_no_dpc_unlock(&g_slab_caches_lock);

    if(cache->cpu_cached) {
        for(size_t i = 0; i < g_init_boot_info->core_count; i++) {
            slab_cpu_cache_t* cc = &cache->cpu_caches[i];
            if(cc->primary != nullptr) {
                slab_cache_destroy_magazine(cache, cc->primary);
            }
            if(cc->secondary != nullptr) {
                slab_cache_destroy_magazine(cache, cc->secondary);
            }
        }
    }

    spinlock_no_dpc_lock(&cache->magazine_lock);
    while(cache->magazine_full_list.count > 0) {
        slab_magazine_t* magazine = CONTAINER_OF(list_pop_front(&cache->magazine_full_list), slab_magazine_t, list_node);
        slab_cache_destroy_magazine(cache, magazine);
    }
    while(cache->magazine_empty_list.count > 0) {
        slab_magazine_t* magazine = CONTAINER_OF(list_pop_front(&cache->magazine_empty_list), slab_magazine_t, list_node);
        slab_cache_destroy_magazine(cache, magazine);
    }
    spinlock_no_dpc_unlock(&cache->magazine_lock);

    spinlock_no_dpc_lock(&cache->slab_lock);
    assert(cache->slab_full_list.count == 0 && "cannot destroy a cache with full slabs");
    assert(cache->slab_partial_list.count == 0 && "cannot destroy a cache with partial slabs");
    if(cache->slab_empty != nullptr) {
        slab_cache_destroy_slab(cache, cache->slab_empty);
        cache->slab_empty = nullptr;
    }
    spinlock_no_dpc_unlock(&cache->slab_lock);

    slab_cache_free(&g_alloc_cache, cache);
}

void slab_reclaim() {
    spinlock_no_dpc_lock(&g_slab_caches_lock);
    LIST_FOR_EACH(&g_slab_caches, node) {
        slab_cache_t* cache = CONTAINER_OF(node, slab_cache_t, list_node);
        spinlock_no_dpc_lock(&cache->slab_lock);
        slab_t* slab = cache->slab_empty;
        cache->slab_empty = nullptr;
        spinlock_no_dpc_unlock(&cache->slab_lock);

        if(slab != nullptr) {
            slab_cache_destroy_slab(cache, slab);
        }
    }
    spinlock_no_dpc_unlock(&g_slab_caches_lock);
}

void* slab_cache_alloc(slab_cache_t* cache) {
    if(!cache->cpu_cached) {
        return slab_cache_alloc_from_slab(cache);
    }

    sched_preempt_disable();
    dpc_disable();

    slab_cpu_cache_t* cc = &cache->cpu_caches[arch_get_core_id()];
    void* ptr = slab_cpu_alloc(cc);
    if(ptr != nullptr) {
        dpc_enable();
        sched_preempt_enable();
        return ptr;
    }
    spinlock_no_dpc_lock(&cache->magazine_lock);

    if(cache->magazine_full_list.count != 0) {
        list_push(&cache->magazine_empty_list, &cc->secondary->list_node);
        cc->secondary = cc->primary;
        cc->primary = CONTAINER_OF(list_pop_front(&cache->magazine_full_list), slab_magazine_t, list_node);
        spinlock_no_dpc_unlock(&cache->magazine_lock);
        ptr = cc->primary->objects[--cc->primary->rounds];
        dpc_enable();
        sched_preempt_enable();
        return ptr;
    }

    spinlock_no_dpc_unlock(&cache->magazine_lock);

    size_t count = slab_cache_alloc_many_from_slab(cache, cc->primary->objects, SLAB_MAGAZINE_SIZE);
    cc->primary->rounds = count;

    if(count == 0) {
        dpc_enable();
        sched_preempt_enable();
        return nullptr;
    }

    ptr = cc->primary->objects[--cc->primary->rounds];

    dpc_enable();
    sched_preempt_enable();
    return ptr;
}

void slab_cache_free(slab_cache_t* cache, void* ptr) {
    if(!cache->cpu_cached) {
        slab_cache_free_to_slab(cache, ptr);
        return;
    }

    sched_preempt_disable();
    dpc_disable();

    slab_cpu_cache_t* cc = &cache->cpu_caches[arch_get_core_id()];
    if(slab_cpu_free(cc, ptr)) {
        dpc_enable();
        sched_preempt_enable();
        return;
    }

    spinlock_no_dpc_lock(&cache->magazine_lock);

    if(cache->magazine_empty_list.count != 0) {
        list_push(&cache->magazine_full_list, &cc->secondary->list_node);
        cc->secondary = cc->primary;
        cc->primary = CONTAINER_OF(list_pop_front(&cache->magazine_empty_list), slab_magazine_t, list_node);
        spinlock_no_dpc_unlock(&cache->magazine_lock);

        cc->primary->objects[cc->primary->rounds++] = ptr;
        dpc_enable();
        sched_preempt_enable();
        return;
    }

    spinlock_no_dpc_unlock(&cache->magazine_lock);


    slab_cache_free_many_to_slab(cache, cc->secondary->objects, cc->secondary->rounds);
    cc->secondary->rounds = 0;

    slab_magazine_t* temp = cc->secondary;
    cc->secondary = cc->primary;
    cc->primary = temp;
    cc->primary->objects[cc->primary->rounds++] = ptr;

    dpc_enable();
    sched_preempt_enable();
}

void slab_init() {
    size_t cache_object_size = sizeof(slab_cache_t) + (g_init_boot_info->core_count * sizeof(slab_cpu_cache_t));
    g_alloc_cache = (slab_cache_t) { .name = "slab-cache",
                                     .object_size = cache_object_size,
                                     .object_align = ALIGN_UP(cache_object_size, alignof(slab_cache_t)),
                                     .slab_size = PAGE_SIZE_DEFAULT * 4,
                                     .slab_align = PAGE_SIZE_DEFAULT * 4,
                                     .slab_lock = SPINLOCK_NO_DPC_INIT,
                                     .slab_full_list = LIST_INIT,
                                     .slab_partial_list = LIST_INIT,
                                     .slab_empty = nullptr,
                                     .magazine_lock = SPINLOCK_NO_DPC_INIT,
                                     .magazine_full_list = LIST_INIT,
                                     .magazine_empty_list = LIST_INIT,
                                     .cpu_cached = false };

    size_t magazine_object_size = sizeof(slab_magazine_t);
    g_alloc_magazine = (slab_cache_t) { .name = "slab-magazine",
                                        .object_size = magazine_object_size,
                                        .object_align = ALIGN_UP(magazine_object_size, alignof(slab_magazine_t)),
                                        .slab_size = PAGE_SIZE_DEFAULT * 2,
                                        .slab_align = PAGE_SIZE_DEFAULT * 2,
                                        .slab_lock = SPINLOCK_NO_DPC_INIT,
                                        .slab_full_list = LIST_INIT,
                                        .slab_partial_list = LIST_INIT,
                                        .slab_empty = nullptr,
                                        .magazine_lock = SPINLOCK_NO_DPC_INIT,
                                        .magazine_full_list = LIST_INIT,
                                        .magazine_empty_list = LIST_INIT,
                                        .cpu_cached = false };

    list_push(&g_slab_caches, &g_alloc_cache.list_node);
    list_push(&g_slab_caches, &g_alloc_magazine.list_node);
}
