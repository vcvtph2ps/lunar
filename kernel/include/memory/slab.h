#pragma once
#include <common/init.h>
#include <common/sync/spinlock.h>
#include <lib/list.h>
#include <lib/types.h>

#define SLAB_MAGAZINE_SIZE 32
#define SLAB_MAGAZINE_EXTRA (g_init_boot_info->core_count * 4)

typedef struct slab slab_t;
typedef struct slab_magazine slab_magazine_t;
typedef struct slab_cpu_cache slab_cpu_cache_t;
typedef struct slab_cache slab_cache_t;

struct slab_magazine {
    void* objects[SLAB_MAGAZINE_SIZE];
    size_t rounds;

    list_node_t list_node;
};

struct slab_cpu_cache {
    spinlock_no_dw_t lock;

    slab_magazine_t* primary;
    slab_magazine_t* secondary;
};

struct slab {
    virt_addr_t buffer;

    size_t free_count;
    void* free_list;

    slab_cache_t* cache;

    list_node_t list_node;
};

struct slab_cache {
    const char* name;
    size_t object_size;
    size_t slab_size;
    size_t slab_align;

    list_node_t list_node;

    spinlock_no_dw_t slab_lock;
    list_t slab_full_list;
    list_t slab_partial_list;
    slab_t* slab_empty;

    spinlock_no_dw_t magazine_lock;
    list_t magazine_full_list;
    list_t magazine_empty_list;

    bool cpu_cached;
    slab_cpu_cache_t cpu_caches[];
};

/**
 * @brief Initialize the slab allocator subsystem. This must be called before any other slab functions are used.
 */
void slab_init();

/**
 * @breif Attempts to reclaim any empty slabs from all caches and return them to the page allocator.
 */
void slab_reclaim();

/**
 * @brief Create a slab cache for objects of a certain size and alignment.
 * @param name Name of the cache (for debugging purposes)
 * @param object_size Size of objects in this cache (must be > 0)
 * @param alignment Alignment of objects in this cache (must be a power of two)
 * @return Pointer to the created slab cache, or nullptr on failure.
 */
slab_cache_t* slab_cache_create(const char* name, size_t object_size, size_t alignment);

/**
 * @brief Destroy a slab cache and free all associated memory. The caller must ensure that no objects allocated from this cache are still in use, as this function will not check for outstanding allocations.
 */
void slab_cache_destroy(slab_cache_t* cache);

/**
 * @brief Allocates an object from the slab cache.
 * @param cache The slab cache to allocate from.
 * @return A pointer to the allocated object, or NULL on failure.
 * @note The returned object is uninitialized and may contain garbage data. The caller is responsible for initializing the object before use. The caller is also responsible for freeing the object using slab_cache_free when it is no longer needed.
 */
void* slab_cache_alloc(slab_cache_t* cache);

/**
 * @brief Frees an object back to the slab cache.
 * @param cache The slab cache to free the object to.
 * @param object The object to free.
 * @note The object must have been allocated from the same slab cache. After this function is called, the object pointer should not be used.
 */
void slab_cache_free(slab_cache_t* cache, void* object);
