#include <arch/cpu_local.h>
#include <common/arch.h>
#include <common/assert.h>
#include <common/interrupts/dw.h>
#include <common/sched/sched.h>
#include <common/sync/spinlock.h>
#include <lib/helpers.h>
#include <lib/list.h>
#include <lib/math.h>
#include <lib/rb.h>
#include <lib/string.h>
#include <memory/pmm.h>
#include <memory/ptm.h>
#include <memory/vm.h>

vm_address_space_t* g_vm_global_address_space;

typedef enum {
    REWRITE_TYPE_DELETE,
    REWRITE_TYPE_PROTECTION,
    REWRITE_TYPE_CACHE
} rewrite_type_t;

static size_t vm_value_of_node(rb_node_t* node) {
    return CONTAINER_OF(node, vm_region_t, region_tree_node)->base;
}

static spinlock_no_dw_t g_region_cache_lock = SPINLOCK_NO_DW_INIT;
static list_t g_region_cache = LIST_INIT;

[[clang::always_inline]] static bool address_in_bounds(virt_addr_t address, virt_addr_t start, virt_addr_t end) {
    return (address >= start) && (address < end);
}

[[clang::always_inline]] static bool segment_in_bounds(virt_addr_t base, size_t length, virt_addr_t start, virt_addr_t end) {
    return (address_in_bounds(base, start, end) && (end - base) >= length);
}

[[clang::always_inline]] static bool address_in_segment(virt_addr_t address, virt_addr_t base, size_t length) {
    return (address >= base && address < (base + length));
}

[[clang::always_inline]] static bool prot_equals(const vm_protection_t p1, const vm_protection_t p2) {
    return p1.read == p2.read && p1.write == p2.write && p1.execute == p2.execute;
}

static vm_region_t* region_insert(vm_address_space_t* address_space, vm_region_t* region);

/// Find last region within a segment.
static vm_region_t* find_region(vm_address_space_t* address_space, uintptr_t address, size_t length) {
    rb_node_t* node = rb_find(&address_space->regions_tree, address + length, RB_SEARCH_TYPE_NEAREST_LT);
    if(node == nullptr) return nullptr;

    vm_region_t* region = CONTAINER_OF(node, vm_region_t, region_tree_node);
    if(region->base + region->length > address) return region;

    return nullptr;
}


/**
 * @brief Find a hole (free space) in an address space satisfying the given alignment.
 * @param align Required alignment (must be a power of two and a multiple of PAGE_SIZE_DEFAULT)
 * @warning Assumes address space lock is acquired.
 * @returns true = hole found, false = not found
 */
static bool find_hole(vm_address_space_t* address_space, uintptr_t address, size_t length, size_t align, uintptr_t* hole) {
    uintptr_t aligned = ALIGN_UP(address, align);
    if(segment_in_bounds(aligned, length, address_space->start, address_space->end) && find_region(address_space, aligned, length) == nullptr) {
        *hole = aligned;
        return true;
    }

    address = ALIGN_UP(address_space->start, align);
    while(true) {
        if(!segment_in_bounds(address, length, address_space->start, address_space->end)) return false;

        vm_region_t* region = find_region(address_space, address, length);
        if(region == nullptr) {
            *hole = address;
            return true;
        }

        address = ALIGN_UP(region->base + region->length, align);
    }
    return false;
}

static bool region_map(vm_region_t* region, uintptr_t address, uintptr_t length) {
    assert(address % PAGE_SIZE_DEFAULT == 0 && length % PAGE_SIZE_DEFAULT == 0);
    assert(address < region->base || address + length >= region->base);

    bool is_global = region->address_space == g_vm_global_address_space;
    switch(region->type) {
        case VM_REGION_TYPE_ANON:
            for(size_t i = 0; i < length; i += PAGE_SIZE_DEFAULT) {
                uintptr_t virtual_address = address + i;
                uintptr_t physical_address = pmm_alloc_page(region->type_data.anon.back_zeroed ? PMM_FLAG_ZERO : PMM_FLAG_NONE);
                if(physical_address == 0) return false;

                if(!ptm_map(region->address_space, virtual_address, physical_address, PAGE_SIZE_DEFAULT, region->protection, region->cache, is_global ? VM_PRIVILEGE_KERNEL : VM_PRIVILEGE_USER, is_global)) {
                    pmm_free_page(physical_address);
                    return false;
                }
            }
            break;
        case VM_REGION_TYPE_DIRECT:
            if(!ptm_map(region->address_space, address, region->type_data.direct.physical_address + (address - region->base), length, region->protection, region->cache, is_global ? VM_PRIVILEGE_KERNEL : VM_PRIVILEGE_USER, is_global)) return false;
            break;
    }
    return true;
}

static void region_unmap(vm_region_t* region, uintptr_t address, uintptr_t length) {
    assert(address % PAGE_SIZE_DEFAULT == 0 && length % PAGE_SIZE_DEFAULT == 0);
    assert(address >= region->base && address + length <= region->base + region->length);

    switch(region->type) {
        case VM_REGION_TYPE_ANON:
            // @todo: unmap phys mem
            break;
        case VM_REGION_TYPE_DIRECT: break;
    }
    ptm_unmap(region->address_space, address, length);
}


/// Check whether the flags of a region are compatible with each other.
static bool regions_mergeable(vm_region_t* left, vm_region_t* right) {
    if(left->type != right->type) return false;
    if(left->base + left->length != right->base) return false;
    if(!prot_equals(left->protection, right->protection)) return false;
    if(left->cache != right->cache) return false;
    if(left->dynamically_backed != right->dynamically_backed) return false;

    switch(left->type) {
        case VM_REGION_TYPE_ANON:
            if(left->type_data.anon.back_zeroed != right->type_data.anon.back_zeroed) return false;
            break;
        case VM_REGION_TYPE_DIRECT:
            if(!segment_in_bounds(left->type_data.direct.physical_address, left->length, 0, ARCH_MEM_PHYS_MAX)) return false;
            if(left->type_data.direct.physical_address + left->length != right->type_data.direct.physical_address) return false;
            break;
    }

    return true;
}

/// Allocate a region from the internal vm region pool.
static vm_region_t* region_alloc(bool global_lock_acquired) {
    sched_preempt_disable();
    dw_status_disable();
    spinlock_nodw_lock(&g_region_cache_lock);
    if(g_region_cache.count == 0) {
        spinlock_nodw_unlock(&g_region_cache_lock);

        phys_addr_t page = pmm_alloc_page(PMM_FLAG_ZERO | PMM_FLAG_PANIC);
        if(!global_lock_acquired) { spinlock_nodw_lock(&g_vm_global_address_space->lock); }

        uintptr_t address;
        if(!find_hole(g_vm_global_address_space, VM_NO_HINT, PAGE_SIZE_DEFAULT, PAGE_SIZE_DEFAULT, &address)) { arch_panic("out of global address space"); }

        if(!ptm_map(g_vm_global_address_space, address, page, PAGE_SIZE_DEFAULT, VM_PROT_RW, VM_CACHE_NORMAL, VM_PRIVILEGE_KERNEL, true)) { arch_panic("failed to map region cache page"); }

        vm_region_t* region = (vm_region_t*) address;
        region[0].address_space = g_vm_global_address_space;
        region[0].type = VM_REGION_TYPE_ANON;
        region[0].base = address;
        region[0].length = PAGE_SIZE_DEFAULT;
        region[0].protection = VM_PROT_RW;
        region[0].cache = VM_CACHE_NORMAL;
        region[0].dynamically_backed = false;

        region_insert(g_vm_global_address_space, &region[0]);
        if(!global_lock_acquired) spinlock_nodw_unlock(&g_vm_global_address_space->lock);

        spinlock_nodw_lock(&g_region_cache_lock);
        for(unsigned int i = 1; i < PAGE_SIZE_DEFAULT / sizeof(vm_region_t); i++) list_push(&g_region_cache, &region[i].region_cache_node);
    }

    list_node_t* node = list_pop(&g_region_cache);
    spinlock_nodw_unlock(&g_region_cache_lock);
    dw_status_enable();
    sched_preempt_enable();
    return CONTAINER_OF(node, vm_region_t, region_cache_node);
}

/// Free region into the internal vm region pool.
static void region_free(vm_region_t* region) {
    spinlock_nodw_lock(&g_region_cache_lock);
    list_push(&g_region_cache, &region->region_cache_node);
    spinlock_nodw_unlock(&g_region_cache_lock);
}

/// Create a region by cloning another to a new region.
static vm_region_t* clone_to(bool global_lock_acquired, uintptr_t base, size_t length, vm_region_t* from) {
    vm_region_t* region = region_alloc(global_lock_acquired);
    region->type = from->type;
    region->address_space = from->address_space;
    region->cache = from->cache;
    region->protection = from->protection;
    region->dynamically_backed = from->dynamically_backed;

    switch(from->type) {
        case VM_REGION_TYPE_ANON: region->type_data.anon.back_zeroed = from->type_data.anon.back_zeroed; break;
        case VM_REGION_TYPE_DIRECT:
            uintptr_t new_physical_address = from->type_data.direct.physical_address;
            if(base > from->base) {
                assert(UINTPTR_MAX - new_physical_address > base - from->base);
                new_physical_address += base - from->base;
            } else {
                assert(new_physical_address > from->base - base);
                new_physical_address -= from->base - base;
            }
            region->type_data.direct.physical_address = new_physical_address;
            break;
    }

    region->base = base;
    region->length = length;
    return region;
}

static vm_region_t* region_insert(vm_address_space_t* address_space, vm_region_t* region) {
    rb_node_t* right_node = rb_find(&address_space->regions_tree, region->base, RB_SEARCH_TYPE_NEAREST_GT);
    if(right_node != nullptr) {
        vm_region_t* right = CONTAINER_OF(right_node, vm_region_t, region_tree_node);
        if(regions_mergeable(region, right)) {
            region->length += right->length;
            rb_remove(&address_space->regions_tree, &right->region_tree_node);
            region_free(right);
        }
    }
    rb_insert(&address_space->regions_tree, &region->region_tree_node);

    rb_node_t* left_node = rb_find(&address_space->regions_tree, region->base, RB_SEARCH_TYPE_NEAREST_LT);
    if(left_node != nullptr) {
        vm_region_t* left = CONTAINER_OF(left_node, vm_region_t, region_tree_node);
        if(regions_mergeable(left, region)) {
            left->length += region->length;
            rb_remove(&address_space->regions_tree, &region->region_tree_node);
            region_free(region);
            return left;
        }
    }

    return region;
}

static vm_region_t* addr_to_region(vm_address_space_t* address_space, uintptr_t address) {
    if(!address_in_bounds(address, address_space->start, address_space->end)) return nullptr;

    rb_node_t* node = rb_find(&address_space->regions_tree, address, RB_SEARCH_TYPE_NEAREST_LTE);
    if(node == nullptr) return nullptr;

    vm_region_t* region = CONTAINER_OF(node, vm_region_t, region_tree_node);
    if(!address_in_segment(address, region->base, region->length)) return nullptr;

    return region;
}

static bool address_space_fix_page(vm_address_space_t* address_space, uintptr_t vaddr) {
    vm_region_t* region = addr_to_region(address_space, vaddr);
    if(region == nullptr || !region->dynamically_backed) return false;
    return region_map(region, MATH_FLOOR(vaddr, PAGE_SIZE_DEFAULT), PAGE_SIZE_DEFAULT);
}

static bool memory_exists(vm_address_space_t* address_space, uintptr_t address, size_t length) {
    if(!address_in_bounds(address, address_space->start, address_space->end) || !address_in_bounds(address + length, address_space->start, address_space->end)) return false;

    uintptr_t top = address + length;
    while(top > address) {
        rb_node_t* node = rb_find(&address_space->regions_tree, top, RB_SEARCH_TYPE_NEAREST_LT);
        if(node == nullptr) return false;

        vm_region_t* region = CONTAINER_OF(node, vm_region_t, region_tree_node);
        if(region->base + region->length < top) return false;

        top = region->base;
    }
    return true;
}

static void rewrite_common(vm_address_space_t* address_space, void* address, size_t length, rewrite_type_t type, vm_protection_t prot, vm_cache_t cache) {
    if(length == 0) return;

    assert((uintptr_t) address % PAGE_SIZE_DEFAULT == 0);
    assert(length % PAGE_SIZE_DEFAULT == 0);
    assert(segment_in_bounds((uintptr_t) address, length, address_space->start, address_space->end));

    spinlock_nodw_lock(&address_space->lock);

    uintptr_t current_address = (uintptr_t) address;

    rb_node_t* node = rb_find(&address_space->regions_tree, current_address, RB_SEARCH_TYPE_NEAREST_LTE);
    if(node != nullptr) {
        vm_region_t* split_region = CONTAINER_OF(node, vm_region_t, region_tree_node);
        if(split_region->base + split_region->length > current_address) {
            uintptr_t split_base = current_address;
            size_t split_length = (split_region->base + split_region->length) - split_base;
            if(split_length > length) split_length = length;

            switch(type) {
                case REWRITE_TYPE_DELETE: region_unmap(split_region, split_base, split_length); goto l_no_clone;
                case REWRITE_TYPE_CACHE:
                    if(split_region->cache == cache) goto l_skip;
                    break;
                case REWRITE_TYPE_PROTECTION:
                    if(prot_equals(split_region->protection, prot)) goto l_skip;
                    break;
            }

            vm_region_t* region = clone_to(address_space == g_vm_global_address_space, split_base, split_length, split_region);

        l_no_clone:

            if(split_region->base + split_region->length > split_base + split_length) {
                uintptr_t new_base = split_base + split_length;
                size_t new_length = (split_region->base + split_region->length) - (split_base + split_length);
                rb_insert(&address_space->regions_tree, &clone_to(address_space == g_vm_global_address_space, new_base, new_length, split_region)->region_tree_node);
            }

            if(split_base > split_region->base) {
                split_region->length = split_base - split_region->base;
            } else {
                rb_remove(&address_space->regions_tree, &split_region->region_tree_node);
                region_free(split_region);
            }

            switch(type) {
                case REWRITE_TYPE_DELETE:     goto l_skip;
                case REWRITE_TYPE_CACHE:      region->cache = cache; break;
                case REWRITE_TYPE_PROTECTION: region->protection = prot; break;
            }

            region = region_insert(address_space, region);

            bool is_global = region->address_space == g_vm_global_address_space;
            ptm_rewrite(region->address_space, split_base, split_length, region->protection, region->cache, is_global ? VM_PRIVILEGE_KERNEL : VM_PRIVILEGE_USER, is_global);

        l_skip:

            current_address = split_base + split_length;
        }
    }

    while(current_address < ((uintptr_t) address + length)) {
        node = rb_find(&address_space->regions_tree, current_address, RB_SEARCH_TYPE_NEAREST_GTE);
        if(node == nullptr) break;

        vm_region_t* split_region = CONTAINER_OF(node, vm_region_t, region_tree_node);
        current_address = split_region->base + split_region->length;

        size_t split_length = ((uintptr_t) address + length) - split_region->base;
        if(split_length > split_region->length) split_length = split_region->length;

        switch(type) {
            case REWRITE_TYPE_DELETE: region_unmap(split_region, split_region->base, split_length); goto r_no_clone;
            case REWRITE_TYPE_CACHE:
                if(split_region->cache == cache) goto r_skip;
                break;
            case REWRITE_TYPE_PROTECTION:
                if(prot_equals(split_region->protection, prot)) goto r_skip;
                break;
        }

        uintptr_t split_base = split_region->base;
        vm_region_t* region = clone_to(address_space == g_vm_global_address_space, split_base, split_length, split_region);

    r_no_clone:

        if(split_region->length > split_length) {
            uintptr_t new_base = split_base + split_length;
            size_t new_length = split_region->length - split_length;
            rb_insert(&address_space->regions_tree, &clone_to(address_space == g_vm_global_address_space, new_base, new_length, split_region)->region_tree_node);
        }
        rb_remove(&address_space->regions_tree, &split_region->region_tree_node);
        region_free(split_region);

        switch(type) {
            case REWRITE_TYPE_DELETE:     goto r_skip;
            case REWRITE_TYPE_CACHE:      region->cache = cache; break;
            case REWRITE_TYPE_PROTECTION: region->protection = prot; break;
        }

        region = region_insert(address_space, region);

        bool is_global = region->address_space == g_vm_global_address_space;
        ptm_rewrite(region->address_space, split_base, split_length, region->protection, region->cache, is_global ? VM_PRIVILEGE_KERNEL : VM_PRIVILEGE_USER, is_global);

    r_skip:
    }
    spinlock_nodw_unlock(&address_space->lock);
}


static void* map_common(vm_address_space_t* address_space, void* hint, size_t length, size_t align, vm_protection_t prot, vm_cache_t cache, vm_flags_t flags, vm_region_type_t type, uintptr_t direct_physical_address) {
    assert(IS_POWER_OF_TWO(align) && align % PAGE_SIZE_DEFAULT == 0);

    uintptr_t address = (uintptr_t) hint;
    assert(address_space != nullptr);
    if((flags & VM_FLAG_FIXED) && address % align != 0) {
        assert(false && "hint must be aligned to the requested alignment when VM_FLAG_FIXED is set");
        return nullptr;
    }
    if(length == 0 || length % PAGE_SIZE_DEFAULT != 0) {
        assertf(false, "length must be non-zero and page aligned %zu", length);
        return nullptr;
    }
    if(address % align != 0) { address = ALIGN_UP(address, align); }

    if(flags & VM_FLAG_FIXED) { rewrite_common(address_space, (void*) address, length, REWRITE_TYPE_DELETE, (vm_protection_t) {}, VM_CACHE_NORMAL); }

    vm_region_t* region = region_alloc(false);
    spinlock_nodw_lock(&address_space->lock);

    if(!(flags & VM_FLAG_FIXED)) {
        bool result = find_hole(address_space, address, length, align, &address);
        if(!result) {
            region_free(region);
            spinlock_nodw_unlock(&address_space->lock);
            return nullptr;
        }
    }

    assert(segment_in_bounds(address, length, address_space->start, address_space->end));
    assert(address % align == 0 && length % PAGE_SIZE_DEFAULT == 0);

    region->address_space = address_space;
    region->type = type;
    region->base = address;
    region->length = length;
    region->protection = prot;
    region->cache = cache;
    region->dynamically_backed = (flags & VM_FLAG_DYNAMICALLY_BACKED) != 0;
    region->shared = (flags & VM_FLAG_SHARED) != 0;

    switch(region->type) {
        case VM_REGION_TYPE_ANON: region->type_data.anon.back_zeroed = (flags & VM_FLAG_ZERO) != 0; break;
        case VM_REGION_TYPE_DIRECT:
            assert(direct_physical_address % PAGE_SIZE_DEFAULT == 0);
            region->type_data.direct.physical_address = direct_physical_address;
            break;
    }

    if(!region->dynamically_backed) {
        if(!region_map(region, region->base, region->length)) {
            region_free(region);
            spinlock_nodw_unlock(&address_space->lock);
            return nullptr;
        }
    }

    region_insert(address_space, region);
    spinlock_nodw_unlock(&address_space->lock);
    return (void*) address;
}

void* vm_map_anon(vm_address_space_t* address_space, void* hint, size_t length, vm_protection_t prot, vm_cache_t cache, vm_flags_t flags) {
    return map_common(address_space, hint, length, PAGE_SIZE_DEFAULT, prot, cache, flags, VM_REGION_TYPE_ANON, 0);
}

void* vm_map_anon_aligned(vm_address_space_t* address_space, void* hint, size_t length, size_t align, vm_protection_t prot, vm_cache_t cache, vm_flags_t flags) {
    return map_common(address_space, hint, length, align, prot, cache, flags, VM_REGION_TYPE_ANON, 0);
}

void* vm_map_direct(vm_address_space_t* address_space, void* hint, size_t length, vm_protection_t prot, vm_cache_t cache, uintptr_t physical_address, vm_flags_t flags) {
    return map_common(address_space, hint, length, PAGE_SIZE_DEFAULT, prot, cache, flags, VM_REGION_TYPE_DIRECT, physical_address);
}

void vm_unmap(vm_address_space_t* address_space, void* address, size_t length) {
    rewrite_common(address_space, address, length, REWRITE_TYPE_DELETE, (vm_protection_t) {}, VM_CACHE_NORMAL);
}

void vm_rewrite_prot(vm_address_space_t* address_space, void* address, size_t length, vm_protection_t prot) {
    rewrite_common(address_space, address, length, REWRITE_TYPE_PROTECTION, prot, VM_CACHE_NORMAL);
}

void vm_rewrite_cache(vm_address_space_t* address_space, void* address, size_t length, vm_cache_t cache) {
    rewrite_common(address_space, address, length, REWRITE_TYPE_CACHE, (vm_protection_t) {}, cache);
}

size_t vm_copy_to(vm_address_space_t* dest_as, uintptr_t dest_addr, void* src, size_t count) {
    if(!memory_exists(dest_as, dest_addr, count)) return 0;
    size_t i = 0;

    uintptr_t src_ptr = (uintptr_t) src;
    while(i < count) {
        size_t offset = (dest_addr + i) % PAGE_SIZE_DEFAULT;
        uintptr_t phys;
        if(!ptm_physical(dest_as, dest_addr + i, &phys)) {
            if(!address_space_fix_page(dest_as, dest_addr + i)) return i;
            bool success = ptm_physical(dest_as, dest_addr + i, &phys);
            assert(success);
        }

        size_t len = math_min(count - i, PAGE_SIZE_DEFAULT - offset);
        memcpy((void*) PTM_TO_HHDM(phys), (void*) src_ptr, len);
        i += len;
        src_ptr += len;
    }
    return i;
}

size_t vm_copy_from(void* dest, vm_address_space_t* src_as, uintptr_t src_addr, size_t count) {
    if(!memory_exists(src_as, src_addr, count)) return 0;
    size_t i = 0;

    uintptr_t dest_ptr = (uintptr_t) dest;
    while(i < count) {
        size_t offset = (src_addr + i) % PAGE_SIZE_DEFAULT;
        uintptr_t phys;
        if(!ptm_physical(src_as, src_addr + i, &phys)) {
            if(!address_space_fix_page(src_as, src_addr + i)) return i;
            bool success = ptm_physical(src_as, src_addr + i, &phys);
            assert(success);
        }

        size_t len = math_min(count - i, PAGE_SIZE_DEFAULT - offset);
        memcpy((void*) dest_ptr, (void*) PTM_TO_HHDM(phys), len);
        i += len;
        dest_ptr += len;
    }
    return i;
}

bool vm_validate_buffer(vm_address_space_t* target_as, virt_addr_t addr, size_t size) {
    if(addr < target_as->start || addr + size > target_as->end) { return false; }

    for(virt_addr_t i = addr; i < addr + size; i += PAGE_SIZE_DEFAULT) {
        phys_addr_t phys;
        if(!ptm_physical(target_as, i, &phys)) {
            if(!address_space_fix_page(target_as, i)) return false;
            bool success = ptm_physical(target_as, i, &phys);
            assert(success);
        }
    }

    return true;
}

rb_tree_t vm_create_regions() {
    return ((rb_tree_t) { .value_of_node = vm_value_of_node, .root = nullptr });
}

static vm_region_t g_kernel_region;
static vm_region_t g_hhdm_region;
static vm_region_t g_pfndb_region;

void vm_init_kernel() {
    extern char kernel_start[];
    extern char kernel_end[];

    virt_addr_t kernel_virt = (virt_addr_t) kernel_start;
    virt_addr_t kernel_size = ((virt_addr_t) kernel_end) - ((virt_addr_t) kernel_start);

    g_kernel_region.address_space = g_vm_global_address_space;
    g_kernel_region.base = kernel_virt;
    g_kernel_region.length = kernel_size;
    g_kernel_region.protection = VM_PROT_RX;
    g_kernel_region.cache = VM_CACHE_NORMAL;
    g_kernel_region.dynamically_backed = false;
    g_kernel_region.type = VM_REGION_TYPE_DIRECT;
    g_kernel_region.type_data.direct.physical_address = 0;

    g_hhdm_region.address_space = g_vm_global_address_space;
    g_hhdm_region.base = g_init_boot_info->hhdm_offset;
    g_hhdm_region.length = g_init_boot_info->hhdm_size;
    g_hhdm_region.protection = VM_PROT_RW;
    g_hhdm_region.cache = VM_CACHE_NORMAL;
    g_hhdm_region.dynamically_backed = false;
    g_hhdm_region.type = VM_REGION_TYPE_DIRECT;
    g_hhdm_region.type_data.direct.physical_address = 0;

    g_pfndb_region.address_space = g_vm_global_address_space;
    g_pfndb_region.base = g_init_boot_info->pfndb_start;
    g_pfndb_region.length = g_init_boot_info->pfndb_size;
    g_pfndb_region.protection = VM_PROT_RW;
    g_pfndb_region.cache = VM_CACHE_NORMAL;
    g_pfndb_region.dynamically_backed = false;
    g_pfndb_region.type = VM_REGION_TYPE_ANON;

    rb_insert(&g_vm_global_address_space->regions_tree, &g_kernel_region.region_tree_node);
    rb_insert(&g_vm_global_address_space->regions_tree, &g_hhdm_region.region_tree_node);
    rb_insert(&g_vm_global_address_space->regions_tree, &g_pfndb_region.region_tree_node);
}
