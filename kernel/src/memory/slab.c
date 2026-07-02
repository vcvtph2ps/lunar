#include <common/assert.h>
#include <common/interrupts/dw.h>
#include <common/sched/sched.h>
#include <common/spinlock.h>
#include <lib/list.h>
#include <memory/slab.h>
#include <memory/vm.h>

static spinlock_no_dw_t g_slab_caches_lock = SPINLOCK_NO_DW_INIT;
static list_t g_slab_caches = LIST_INIT;

static slab_cache_t g_alloc_cache;
static slab_cache_t g_alloc_magazine;

static slab_t* slab_cache_create_slab(slab_cache_t* cache) {
    virt_addr_t block = (virt_addr_t) vm_map_anon_aligned(g_vm_global_address_space, VM_NO_HINT, cache->slab_size, cache->slab_align, VM_PROT_RW, VM_CACHE_NORMAL, VM_FLAG_ZERO);
    slab_t* slab = (slab_t*) ((block + cache->slab_size) - sizeof(slab_t));
    slab->cache = cache;
    slab->buffer = block;
    slab->free_list = nullptr;
    slab->free_count = 0;

    size_t slab_size = cache->slab_size - sizeof(slab_t);
    assert(slab_size / cache->object_size > 0);

    // Initialize the free list for the slab.
    size_t object_count = slab_size / cache->object_size;
    for(size_t i = 0; i < object_count; i++) {
        size_t object_offset = i * cache->object_size;
        void** obj = (void**) (block + object_offset);
        *obj = slab->free_list;
        slab->free_list = obj;
        assert(slab->free_list != nullptr);
        slab->free_count++;
    }

    return slab;
}

static void slab_cache_destroy_slab(slab_cache_t* cache, slab_t* slab) {
    assert(slab != nullptr);
    assert(cache != nullptr);
    assert(slab->cache == cache);
    vm_unmap(g_vm_global_address_space, (void*) slab->buffer, cache->slab_size);
}

static void slab_cache_free_to_slab(slab_cache_t* cache, void* ptr) {
    slab_t* slab = (slab_t*) (ALIGN_DOWN((uintptr_t) ptr, cache->slab_align) + cache->slab_size - sizeof(slab_t));

    spinlock_nodw_lock(&cache->slab_lock);

    if(slab->free_count == 0) {
        list_node_delete(&cache->slab_full_list, &slab->list_node);
        list_push(&cache->slab_partial_list, &slab->list_node);
    }

    if(slab->free_count + 1 == (cache->slab_size - sizeof(slab_t)) / cache->object_size) {
        list_node_delete(&cache->slab_partial_list, &slab->list_node);

        if(cache->slab_empty) { slab_cache_destroy_slab(cache, cache->slab_empty); }
        cache->slab_empty = slab;
    }

    *(void**) ptr = slab->free_list;
    assert(ptr != nullptr);
    slab->free_list = ptr;
    slab->free_count++;

    spinlock_nodw_unlock(&cache->slab_lock);
}

void slab_cache_destroy(slab_cache_t* cache) {
    if(!cache) return;

    spinlock_nodw_lock(&g_slab_caches_lock);
    list_node_delete(&g_slab_caches, &cache->list_node);
    spinlock_nodw_unlock(&g_slab_caches_lock);

    if(cache->cpu_cached) {
        for(size_t i = 0; i < g_init_boot_info->core_count; i++) {
            slab_cpu_cache_t* cc = &cache->cpu_caches[i];
            spinlock_nodw_lock(&cc->lock);
            if(cc->primary) {
                for(size_t j = 0; j < cc->primary->rounds; j++) { slab_cache_free_to_slab(cache, cc->primary->objects[j]); }
                cc->primary->rounds = 0;
                slab_cache_free(&g_alloc_magazine, cc->primary);
                cc->primary = nullptr;
            }
            if(cc->secondary) {
                for(size_t j = 0; j < cc->secondary->rounds; j++) { slab_cache_free_to_slab(cache, cc->secondary->objects[j]); }
                cc->secondary->rounds = 0;
                slab_cache_free(&g_alloc_magazine, cc->secondary);
                cc->secondary = nullptr;
            }
            spinlock_nodw_unlock(&cc->lock);
        }
    }

    spinlock_nodw_lock(&cache->magazine_lock);
    while(cache->magazine_full_list.count > 0) {
        slab_magazine_t* magazine = CONTAINER_OF(list_pop_front(&cache->magazine_full_list), slab_magazine_t, list_node);
        for(size_t i = 0; i < magazine->rounds; i++) { slab_cache_free_to_slab(cache, magazine->objects[i]); }
        magazine->rounds = 0;
        slab_cache_free(&g_alloc_magazine, magazine);
    }

    while(cache->magazine_empty_list.count > 0) {
        slab_magazine_t* magazine = CONTAINER_OF(list_pop_front(&cache->magazine_empty_list), slab_magazine_t, list_node);
        slab_cache_free(&g_alloc_magazine, magazine);
    }

    spinlock_nodw_unlock(&cache->magazine_lock);

    spinlock_nodw_lock(&cache->slab_lock);
    assert(cache->slab_full_list.count == 0 && "Attempted to destroy slab cache with full slabs (leaked objects)");
    assert(cache->slab_partial_list.count == 0 && "Attempted to destroy slab cache with partial slabs (leaked objects)");

    if(cache->slab_empty) {
        slab_cache_destroy_slab(cache, cache->slab_empty);
        cache->slab_empty = nullptr;
    }
    spinlock_nodw_unlock(&cache->slab_lock);

    slab_cache_free(&g_alloc_cache, cache);
}

static void* slab_cache_alloc_from_slab(slab_cache_t* cache) {
    spinlock_nodw_lock(&cache->slab_lock);

    if(cache->slab_partial_list.count == 0) {
        if(cache->slab_empty) {
            list_push(&cache->slab_partial_list, &cache->slab_empty->list_node);
            cache->slab_empty = nullptr;
        } else {
            spinlock_nodw_unlock(&cache->slab_lock);
            slab_t* new_slab = slab_cache_create_slab(cache);
            spinlock_nodw_lock(&cache->slab_lock);
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

    spinlock_nodw_unlock(&cache->slab_lock);
    return ptr;
}


void* slab_cache_alloc(slab_cache_t* cache) {
    if(!cache->cpu_cached) { return slab_cache_alloc_from_slab(cache); }

    sched_preempt_disable();
    dw_status_disable();
    slab_cpu_cache_t* cc = &cache->cpu_caches[arch_get_core_id()];

    (void) spinlock_nodw_lock(&cc->lock);

    if(cc->primary->rounds > 0) {
        void* ptr = cc->primary->objects[--cc->primary->rounds];
        spinlock_nodw_unlock(&cc->lock);
        dw_status_enable();
        sched_preempt_enable();
        return ptr;
    }

    if(cc->secondary->rounds == SLAB_MAGAZINE_SIZE) {
        slab_magazine_t* temp = cc->secondary;
        cc->secondary = cc->primary;
        cc->primary = temp;

        void* ptr = cc->primary->objects[--cc->primary->rounds];
        spinlock_nodw_unlock(&cc->lock);
        dw_status_enable();
        sched_preempt_enable();
        return ptr;
    }

    spinlock_nodw_lock(&cache->magazine_lock);

    if(cache->magazine_full_list.count != 0) {
        list_push(&cache->magazine_empty_list, &cc->secondary->list_node);
        cc->secondary = cc->primary;
        cc->primary = CONTAINER_OF(list_pop_front(&cache->magazine_full_list), slab_magazine_t, list_node);
        spinlock_nodw_unlock(&cache->magazine_lock);
        void* ptr = cc->primary->objects[--cc->primary->rounds];
        spinlock_nodw_unlock(&cc->lock);
        dw_status_enable();
        sched_preempt_enable();
        return ptr;
    }

    spinlock_nodw_unlock(&cache->magazine_lock);
    spinlock_nodw_unlock(&cc->lock);

    void* ptr = slab_cache_alloc_from_slab(cache);
    dw_status_enable();
    sched_preempt_enable();
    return ptr;
}

void slab_cache_free(slab_cache_t* cache, void* ptr) {
    if(!cache->cpu_cached) {
        slab_cache_free_to_slab(cache, ptr);
        return;
    }

    sched_preempt_disable();
    dw_status_disable();
    slab_cpu_cache_t* cc = &cache->cpu_caches[arch_get_core_id()];

    spinlock_nodw_lock(&cc->lock);

    if(cc->primary->rounds < SLAB_MAGAZINE_SIZE) {
        cc->primary->objects[cc->primary->rounds++] = ptr;
        spinlock_nodw_unlock(&cc->lock);
        dw_status_enable();
        sched_preempt_enable();
        return;
    }

    if(cc->secondary->rounds == 0) {
        slab_magazine_t* temp = cc->secondary;
        cc->secondary = cc->primary;
        cc->primary = temp;

        cc->primary->objects[cc->primary->rounds++] = ptr;
        spinlock_nodw_unlock(&cc->lock);
        dw_status_enable();
        sched_preempt_enable();
        return;
    }

    spinlock_nodw_lock(&cache->magazine_lock);

    if(cache->magazine_empty_list.count != 0) {
        list_push(&cache->magazine_full_list, &cc->secondary->list_node);
        cc->secondary = cc->primary;
        cc->primary = CONTAINER_OF(list_pop_front(&cache->magazine_empty_list), slab_magazine_t, list_node);
        spinlock_nodw_unlock(&cache->magazine_lock);

        cc->primary->objects[cc->primary->rounds++] = ptr;
        spinlock_nodw_unlock(&cc->lock);
        dw_status_enable();
        sched_preempt_enable();
        return;
    }

    spinlock_nodw_unlock(&cache->magazine_lock);
    spinlock_nodw_unlock(&cc->lock);

    slab_cache_free_to_slab(cache, ptr);
    dw_status_enable();
    sched_preempt_enable();
}

slab_cache_t* slab_cache_create(const char* name, size_t object_size, size_t alignment) {
    assert(object_size >= 8 && "object_size must be greater than 8 bytes");
    assert(alignment > 0 && "alignment must be greater than 0");

    slab_cache_t* cache = slab_cache_alloc_from_slab(&g_alloc_cache);
    cache->name = name;
    cache->object_size = object_size;
    cache->slab_size = PAGE_SIZE_DEFAULT * 4;
    cache->slab_align = PAGE_SIZE_DEFAULT * 4;
    cache->cpu_cached = true;

    cache->slab_lock = SPINLOCK_NO_DW_INIT;
    cache->slab_full_list = LIST_INIT;
    cache->slab_partial_list = LIST_INIT;

    cache->magazine_lock = SPINLOCK_NO_DW_INIT;
    cache->magazine_full_list = LIST_INIT;
    cache->magazine_empty_list = LIST_INIT;

    for(size_t i = 0; i < SLAB_MAGAZINE_EXTRA; i++) {
        slab_magazine_t* magazine = slab_cache_alloc_from_slab(&g_alloc_magazine);
        magazine->rounds = 0;
        for(size_t j = 0; j < SLAB_MAGAZINE_SIZE; j++) magazine->objects[j] = nullptr;
        list_push(&cache->magazine_empty_list, &magazine->list_node);
    }

    if(cache->cpu_cached) {
        for(size_t i = 0; i < g_init_boot_info->core_count; i++) {
            slab_magazine_t* magazine_primary = slab_cache_alloc_from_slab(&g_alloc_magazine);
            magazine_primary->rounds = SLAB_MAGAZINE_SIZE;
            for(size_t j = 0; j < SLAB_MAGAZINE_SIZE; j++) magazine_primary->objects[j] = slab_cache_alloc_from_slab(cache);

            slab_magazine_t* magazine_secondary = slab_cache_alloc_from_slab(&g_alloc_magazine);
            magazine_secondary->rounds = 0;
            for(size_t j = 0; j < SLAB_MAGAZINE_SIZE; j++) magazine_secondary->objects[j] = nullptr;

            cache->cpu_caches[i].lock = SPINLOCK_NO_DW_INIT;
            cache->cpu_caches[i].primary = magazine_primary;
            cache->cpu_caches[i].secondary = magazine_secondary;
        }
    }

    spinlock_nodw_lock(&g_slab_caches_lock);
    list_push(&g_slab_caches, &cache->list_node);
    spinlock_nodw_unlock(&g_slab_caches_lock);

    return cache;
}

void slab_reclaim() {
    spinlock_nodw_lock(&g_slab_caches_lock);
    LIST_FOR_EACH(&g_slab_caches, node) {
        slab_cache_t* cache = CONTAINER_OF(node, slab_cache_t, list_node);
        spinlock_nodw_lock(&cache->slab_lock);
        if(cache->slab_empty) {
            slab_cache_destroy_slab(cache, cache->slab_empty);
            cache->slab_empty = nullptr;
        }
        spinlock_nodw_unlock(&cache->slab_lock);
    }
    spinlock_nodw_unlock(&g_slab_caches_lock);
}

void slab_init() {
    g_alloc_cache = (slab_cache_t) { .name = "slab-cache",
                                     .object_size = sizeof(slab_cache_t) + (g_init_boot_info->core_count * sizeof(slab_cpu_cache_t)),
                                     .slab_size = PAGE_SIZE_DEFAULT * 4,
                                     .slab_align = PAGE_SIZE_DEFAULT * 4,
                                     .slab_lock = SPINLOCK_NO_DW_INIT,
                                     .slab_full_list = LIST_INIT,
                                     .slab_partial_list = LIST_INIT,
                                     .slab_empty = nullptr,
                                     .magazine_lock = SPINLOCK_NO_DW_INIT,
                                     .magazine_full_list = LIST_INIT,
                                     .magazine_empty_list = LIST_INIT,
                                     .cpu_cached = false };

    g_alloc_magazine = (slab_cache_t) { .name = "slab-magazine",
                                        .object_size = sizeof(slab_magazine_t) + (SLAB_MAGAZINE_SIZE * sizeof(void*)),
                                        .slab_size = PAGE_SIZE_DEFAULT * 2,
                                        .slab_align = PAGE_SIZE_DEFAULT * 2,
                                        .slab_lock = SPINLOCK_NO_DW_INIT,
                                        .slab_full_list = LIST_INIT,
                                        .slab_partial_list = LIST_INIT,
                                        .slab_empty = nullptr,
                                        .magazine_lock = SPINLOCK_NO_DW_INIT,
                                        .magazine_full_list = LIST_INIT,
                                        .magazine_empty_list = LIST_INIT,
                                        .cpu_cached = false };

    list_push(&g_slab_caches, &g_alloc_cache.list_node);
    list_push(&g_slab_caches, &g_alloc_magazine.list_node);
}
