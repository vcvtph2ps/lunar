#pragma once
#include <lib/hashmap.h>
#include <lib/helpers.h>
#include <lib/list.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct vfs_page_cache_object vfs_page_cache_object_t; // NOLINT
typedef struct page_cache_page page_cache_page_t;

typedef enum {
    /// The page is resident in the cache but its contents are not valid yet
    PAGE_CACHE_PAGE_LOADING,
    /// The page is resident and its contents may be read
    PAGE_CACHE_PAGE_READY,
    /// A previous attempt to populate the page failed
    PAGE_CACHE_PAGE_FAILED,
} page_cache_page_state_t;

/**
 * @brief Callback used to populate a cached page on demand
 * @param context The opaque context stored in the owning vfs_page_cache_object_t
 * @param index The zero based page index within the object that should be filled
 * @param dest A kernel writable buffer of PAGE_SIZE_DEFAULT bytes to fill
 * @return true on success, false if the page could not be populated
 * @note This is invoked with the global page cache lock dropped, but it may not block
 */
typedef bool (*page_cache_fill_fn)(void* context, uint64_t index, void* dest);

/**
 * @brief Callback used to write a dirty cached page back to its backing store
 * @param context The opaque context stored in the owning vfs_page_cache_object_t
 * @param index The zero based page index within the object to write back
 * @param src A kernel readable buffer of PAGE_SIZE_DEFAULT bytes holding the page contents
 * @return true on success, false if the page could not be written back
 * @note This is invoked with the global page cache lock held and therefore may not block
 */
typedef bool (*page_cache_writeback_fn)(void* context, uint64_t index, const void* src);

/**
 * @brief an object that can have its contents cached a page at a time
 * @note the object references the backing store and knows how to populate and writeback pages via `context`, but does not own any storage
 */
struct vfs_page_cache_object {
    void* context;

    /// Callback used to read pagess
    page_cache_fill_fn fill;

    /// Callback used to flush dirty pages, or nullptr if the object is read-only
    page_cache_writeback_fn writeback;

    /// The size of the object in bytes
    size_t size;
};

/**
 * @brief A single cached page
 * @note Instances are owned by the global page cache and must only be manipulated through the page cache api
 */
struct page_cache_page {
    vfs_page_cache_object_t* object;
    uint64_t index;

    /// The physical address of the page backing this cache entry
    uintptr_t physical;

    /// Current state of the page
    ATOMIC page_cache_page_state_t state;

    /// Reference count held while the page is in use by a caller
    ATOMIC uint32_t pins;

    /// Set when the page contents have been changed and need writing back
    ATOMIC bool dirty;

    /// Whether this page currently lives on the global LRU list
    bool in_lru;

    hashmap_node_t hash_node;
    list_node_t lru_node;
};

/// @brief Initializes the global page cache
void page_cache_init(void);

/**
 * @brief Creates a new page cache object
 * @param context Opaque context passed to the fill and writeback callbacks
 * @param fill Callback used to populate pages on demand
 * @param writeback Callback used to flush dirty pages, or nullptr for read-only objects
 * @param size The size of the object in bytes
 * @return The new object, or nullptr on allocation failure
 */
vfs_page_cache_object_t* page_cache_object_create(void* context, page_cache_fill_fn fill, page_cache_writeback_fn writeback, size_t size);

/**
 * @brief Destroys a page cache object, writing back dirty pages and evicting every evictable page it owns
 * @param object The object to destroy
 * @note Pages that are still pinned or mapped cannot be evicted and are leaked, callers must ensure the object is no longer reachable before calling this
 */
void page_cache_object_destroy(vfs_page_cache_object_t* object);

/**
 * @brief Looks up a page, populating it from the backing store if necessary
 * @param object The object the page belongs to
 * @param index The zero based page index to fetch
 * @return A page with the contents valid and a reference held, or nullptr if the page is out of range or could not be populated
 * @note The returned page is pinned, the caller must release it with page_cache_put() once it is done with the physical page
 */
page_cache_page_t* page_cache_get(vfs_page_cache_object_t* object, uint64_t index);

/**
 * @brief Looks up a resident page without populating it or taking a reference
 * @param object The object the page belongs to
 * @param index The zero based page index to look up
 * @return The resident page, or nullptr if it is not currently cached
 * @note Callers must hold some other guarantee that the page cannot be evicted (e.g. it is currently mapped into a page table) before dereferencing the result
 */
page_cache_page_t* page_cache_peek(vfs_page_cache_object_t* object, uint64_t index);

/// @brief Releases the transient reference acquired by page_cache_get()
void page_cache_put(page_cache_page_t* page);

/// @brief Marks a cached page as dirty so it will be written back before eviction
void page_cache_mark_dirty(page_cache_page_t* page);

/**
 * @brief Writes into a cached page, populating it first if necessary
 * @param object The object the page belongs to
 * @param index The zero based page index to write into
 * @param src The source buffer in the global address space
 * @param offset The byte offset within the page to start writing at
 * @param count The number of bytes to write `offset + count` must not exceed the page size
 * @return true on success, false if the page could not be populated
 * @note The page is marked dirty; it is written back to the backing store either explicitly or when it is evicted
 */
bool page_cache_write(vfs_page_cache_object_t* object, uint64_t index, const void* src, size_t offset, size_t count);

/**
 * @brief Writes back every dirty page belonging to an object
 * @param object The object to flush
 * @return The number of pages written back
 */
size_t page_cache_writeback(vfs_page_cache_object_t* object);

/**
 * @brief Marks a cached page as unreadable, forcing the next access to reload it
 * @param object The object the page belongs to
 * @param index The zero based page index to invalidate
 * @return true if the page was evicted, false if it was absent or still mapped
 * @note This drops the page from the cache if it is not currently mapped into any address space The next fault will repopulate it through the fill callback
 * @note The caller must first handle any page table entries pointing to the page
 */
bool page_cache_mark_unreadable(vfs_page_cache_object_t* object, uint64_t index);

/**
 * @brief Evicts up to `target_pages` resident, unreferenced pages
 * @param target_pages The maximum number of pages to evict
 * @return The number of pages actually evicted
 * @note Pages that are pinned or mapped in a page table are skipped and moved to the most recently used end of the LRU list
 */
size_t page_cache_reclaim(size_t target_pages);

/**
 * @brief Returns the number of resident pages in the global cache
 */
size_t page_cache_count(void);
