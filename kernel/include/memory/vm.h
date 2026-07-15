#pragma once
#include <common/sync/spinlock.h>
#include <lib/list.h>
#include <lib/rb.h>
#include <lib/types.h>
#include <stddef.h>

typedef enum {
    VM_REGION_TYPE_ANON,
    VM_REGION_TYPE_DIRECT
} vm_region_type_t;

typedef enum {
    VM_FAULT_UKKNOWN = 0,
    VM_FAULT_NOT_PRESENT,
} vm_fault_reason_t;

typedef struct {
    bool read    : 1;
    bool write   : 1;
    bool execute : 1;
} vm_protection_t;

#define VM_PROT_NO_ACCESS ((vm_protection_t) { .read = 0, .write = 0, .execute = 0 })
#define VM_PROT_RO ((vm_protection_t) { .read = 1, .write = 0, .execute = 0 })
#define VM_PROT_RW ((vm_protection_t) { .read = 1, .write = 1, .execute = 0 })
#define VM_PROT_RX ((vm_protection_t) { .read = 1, .write = 0, .execute = 1 })
#define VM_PROT_RWX ((vm_protection_t) { .read = 1, .write = 1, .execute = 1 })

typedef enum {
    VM_PRIVILEGE_KERNEL,
    VM_PRIVILEGE_USER,
} vm_privilege_t;

typedef enum {
    VM_CACHE_NORMAL,
    VM_CACHE_DISABLE,
    VM_CACHE_WRITE_THROUGH,
    VM_CACHE_WRITE_COMBINE
} vm_cache_t;

#define VM_FLAG_NONE 0
#define VM_FLAG_FIXED (1 << 1)
#define VM_FLAG_DYNAMICALLY_BACKED (1 << 2)
#define VM_FLAG_MMIO (1 << 3) /* physical address is device memory */
#define VM_FLAG_ZERO (1 << 10) /* only applies to anonymous mappings */

#define VM_NO_HINT 0

typedef struct vm_address_space vm_address_space_t;

typedef struct {
    vm_address_space_t* address_space;

    uintptr_t base;
    size_t length;

    vm_region_type_t type;
    vm_cache_t cache;

    vm_protection_t protection;
    bool dynamically_backed : 1;
    bool shared             : 1;
    bool mmio               : 1;

    rb_node_t region_tree_node;
    list_node_t region_cache_node;

    union {
        struct {
            bool back_zeroed;
        } anon;
        struct {
            uintptr_t physical_address;
        } direct;
    } type_data;
} vm_region_t;

typedef struct {
    spinlock_no_dw_t ptm_lock;
    phys_addr_t top_level_page_table;
} vm_ptm_t;

struct vm_address_space {
    bool is_user;
    vm_ptm_t ptm;

    virt_addr_t start, end;
    rb_tree_t regions_tree;
    spinlock_no_dw_t lock;
};

#define VM_FLAG_NONE 0
#define VM_FLAG_FIXED (1 << 1)
#define VM_FLAG_DYNAMICALLY_BACKED (1 << 2)
#define VM_FLAG_SHARED (1 << 3)
#define VM_FLAG_ZERO (1 << 10) /* only applies to anonymous mappings */

typedef uint64_t vm_flags_t;

extern vm_address_space_t* g_vm_global_address_space;

/**
 * @brief Maps a new region of memory into the given address space, returning the base virtual address of the new mapping. If hint is not VM_NO_HINT, the kernel will attempt to place the mapping at the given virtual address, but may choose a
 * different address if that one is unavailable or if VM_FLAG_FIXED is not set.
 * @param address_space The address space to map into
 * @param hint The desired page aligned virtual address to place the mapping at, or VM_NO_HINT to allow the kernel to choose the virtual address freely
 * @param length The length of the mapping, in bytes. Must be greater than 0 and page-aligned.
 * @param prot The desired protection flags for the mapping
 * @param cache The desired caching behavior for the mapping
 * @param flags Additional flags for the mapping. See VM_FLAG_* constants for possible values.
 * @return The base virtual address of the new mapping, or NULL if the mapping failed (e.g. due to invalid parameters or lack of available virtual address space). Note that the kernel may choose a different virtual address than the one specified by
 */
void* vm_map_anon(vm_address_space_t* address_space, void* hint, size_t length, vm_protection_t prot, vm_cache_t cache, vm_flags_t flags);

/**
 * @brief Same as vm_map_anon, but allows specifying an alignment for the mapping. The kernel will attempt to place the mapping at a virtual address that is a multiple of align, but may choose a different aligned address if that one is unavailable or
 * if VM_FLAG_FIXED is not set.
 * @param address_space The address space to map into
 * @param hint The desired page aligned virtual address to place the mapping at, or VM_NO_HINT to allow the kernel to choose the virtual address freely
 * @param length The length of the mapping, in bytes. Must be greater than 0 and page-aligned.
 * @param align The desired alignment for the mapping, in bytes. Must be a power of two and at least the page size.
 * @param prot The desired protection flags for the mapping
 * @param cache The desired caching behavior for the mapping
 * @param flags Additional flags for the mapping. See VM_FLAG_* constants for possible values.
 * @return The base virtual address of the new mapping, or NULL if the mapping failed (e.g. due to invalid parameters or lack of available virtual address space). Note that the kernel may choose a different virtual address than the one specified by
 */
void* vm_map_anon_aligned(vm_address_space_t* address_space, void* hint, size_t length, size_t align, vm_protection_t prot, vm_cache_t cache, vm_flags_t flags);

/**
 * @brief Maps a physical address directly to a virtual allocation. The kernel will attempt to place the mapping at a virtual address that is a multiple of align, but may choose a different aligned address if that one is unavailable or
 * if VM_FLAG_FIXED is not set.
 * @param address_space The address space to map into
 * @param hint The desired page aligned virtual address to place the mapping at, or VM_NO_HINT to allow the kernel to choose the virtual address freely
 * @param length The length of the mapping, in bytes. Must be greater than 0
 * @param prot The desired protection flags for the mapping
 * @param cache The desired caching behavior for the mapping
 * @param physical_address The physical address to map. Must be page-aligned.
 * @param flags Additional flags for the mapping. See VM_FLAG_* constants for possible values.
 * @return The base virtual address of the new mapping, or NULL if the mapping failed (e.g. due to invalid parameters or lack of available virtual address space). Note that the kernel may choose a different virtual address than the one specified by
 * hint, if the requested address is unavailable.
 */
void* vm_map_direct(vm_address_space_t* address_space, void* hint, size_t length, vm_protection_t prot, vm_cache_t cache, uintptr_t physical_address, vm_flags_t flags);

/**
 * @brief Unmaps a region of memory previously mapped into the given address space. The region to unmap is specified by the starting virtual address and length, and must be fully contained within an existing mapping in the address space.
 * @param address_space The address space to unmap from
 * @param address The starting virtual address of the region to unmap. Must be page-aligned and within the address space
 * @param length The length of the region to unmap, in bytes. Must be greater than 0, page-aligned, and the region specified by address and length must be fully contained within an existing mapping in the address space.
 */
void vm_unmap(vm_address_space_t* address_space, void* address, size_t length);

/**
 * @brief Changes the protection flags of an existing mapping in the given address space. The region to change is specified by the starting virtual address and length, and must be fully contained within an existing mapping in the address space.
 * @param address_space The address space to change the mapping in
 * @param address The starting virtual address of the region to change. Must be page-aligned
 * @param length The length of the region to change, in bytes. Must be greater than 0, page-aligned, and the region specified by address and length must be fully contained within an existing mapping in the address space.
 * @param prot The new protection flags for the mapping
 */
void vm_rewrite_prot(vm_address_space_t* address_space, void* address, size_t length, vm_protection_t prot);

/**
 * @brief Changes the cache flags of an existing mapping in the given address space. The region to change is specified by the starting virtual address and length, and must be fully contained within an existing mapping in the address space.
 * @param address_space The address space to change the mapping in
 * @param address The starting virtual address of the region to change. Must be page-aligned
 * @param length The length of the region to change, in bytes. Must be greater than 0, page-aligned, and the region specified by address and length must be fully contained within an existing mapping in the address space.
 * @param cache The new cache flags for the mapping
 */
void vm_rewrite_cache(vm_address_space_t* address_space, void* address, size_t length, vm_cache_t cache);


/**
 * @brief Handles a page fault that occurred in the given address space at the specified virtual address, with the given fault reason.
 */
bool vm_fault(uintptr_t address, vm_fault_reason_t fault);

/**
 * @brief Copies data from the global address space into the specified address space, handling any necessary page faults that may occur during the copy.
 * @param dest_as The address space to copy into
 * @param dest_addr The virtual address in dest_as to copy into. Must be page-aligned and within the address space.
 * @param src The source buffer in the global address space to copy from
 * @param count The number of bytes to copy. Must be greater than 0 and page-aligned, and the region specified by dest_addr and count must be fully contained within an existing mapping in dest_as.
 * @return The number of bytes successfully copied. This may be less than count if a page fault occurred during the copy and was not handled successfully by vm_fault.
 */
size_t vm_copy_to(vm_address_space_t* dest_as, uintptr_t dest_addr, void* src, size_t count);

/**
 * @brief Copies data to the global address space from the specified address space, handling any necessary page faults that may occur during the copy.
 * @param dest The destination buffer in the global address space to copy into
 * @param src_as The address space to copy from
 * @param src_addr The virtual address in src_as to copy from. Must be page-aligned and within the address space.
 * @param count The number of bytes to copy. Must be greater than 0 and page-aligned, and the region specified by src_addr and count must be fully contained within an existing mapping in src_as.
 * @return The number of bytes successfully copied. This may be less than count if a page fault occurred during the copy and was not handled successfully by vm_fault.
 */
size_t vm_copy_from(void* dest, vm_address_space_t* src_as, uintptr_t src_addr, size_t count);

/**
 * @brief Validates that the given buffer is a valid pointer to a buffer of the specified size in the given address space
 * @param target_as The address space to validate against
 * @param buf The virtual address of the start of the buffer
 * @param count The size of the buffer in bytes
 */
bool vm_validate_buffer(vm_address_space_t* target_as, virt_addr_t buf, size_t count);

/**
 * @brief Creates and initializes a new rb tree to be used for managing vm_region_t structures in an address space.
 * @return A new rb_tree_t instance that is ready to be used for managing vm_region_t structures in an address space.
 */
rb_tree_t vm_create_regions();

/**
 * @brief Initializes kernel vm regions
 */
void vm_init_kernel();
