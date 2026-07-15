#pragma once
#include <common/memory.h>
#include <lib/types.h>
#include <memory/vm.h>
#include <stdbool.h>
#include <stdint.h>

typedef uint64_t arch_pte_entry_t;

#define ARCH_PTE_MAX_LEVELS 5

extern bool g_arch_pte_la57_enabled;

#define ARCH_PTE_PRESENT ((uint64_t) (1u << 0))
#define ARCH_PTE_RW ((uint64_t) (1u << 1))
#define ARCH_PTE_USER ((uint64_t) (1u << 2))
#define ARCH_PTE_WRITETHROUGH ((uint64_t) (1u << 3))
#define ARCH_PTE_DISABLECACHE ((uint64_t) (1u << 4))
#define ARCH_PTE_ACCESSED ((uint64_t) (1u << 5))
#define ARCH_PTE_DIRTY ((uint64_t) (1u << 6))
#define ARCH_PTE_LARGE ((uint64_t) (1u << 7))
#define ARCH_PTE_GLOBAL ((uint64_t) (1u << 8))
#define ARCH_PTE_PAT_4K ((uint64_t) (1u << 7))
#define ARCH_PTE_PAT_LARGE ((uint64_t) (1u << 12))
#define ARCH_PTE_NX ((uint64_t) 1u << 63)

#define ARCH_PTE_ADDR_MASK ((uint64_t) 0x000FFFFFFFFFF000u)
#define ARCH_PTE_ADDR_MASK_2M ((uint64_t) 0x000FFFFFFFE00000u)
#define ARCH_PTE_ADDR_MASK_1G ((uint64_t) 0x000FFFFFC0000000u)

static inline uint64_t pte_addr_mask(uint32_t level) {
    if(level == 2u) return ARCH_PTE_ADDR_MASK_2M;
    if(level == 3u) return ARCH_PTE_ADDR_MASK_1G;
    return ARCH_PTE_ADDR_MASK;
}

static inline uint64_t pte_pat_bit(uint32_t level) {
    return level == 1u ? ARCH_PTE_PAT_4K : ARCH_PTE_PAT_LARGE;
}

static inline uint64_t pte_cache_flags(vm_cache_t cache, uint32_t level) {
    switch(cache) {
        case VM_CACHE_NORMAL:        return 0; // PAT0 = WB
        case VM_CACHE_WRITE_THROUGH: return ARCH_PTE_WRITETHROUGH; // PAT1 = WT
        case VM_CACHE_DISABLE:       return ARCH_PTE_DISABLECACHE | ARCH_PTE_WRITETHROUGH; // PAT3 = UC
        case VM_CACHE_WRITE_COMBINE: return pte_pat_bit(level); // PAT4 = WC
    }
    __builtin_unreachable();
}

static inline uint64_t pte_priv_flags(vm_privilege_t priv) {
    return priv == VM_PRIVILEGE_USER ? ARCH_PTE_USER : 0u;
}

static inline uint32_t pte_top_level() {
    return g_arch_pte_la57_enabled ? 5u : 4u;
}

static inline size_t pte_level_shift(uint32_t level) {
    return 12u + (level - 1u) * 9u;
}

static inline size_t pte_level_page_size(uint32_t level) {
    return (size_t) 1u << pte_level_shift(level);
}

static inline uint32_t pte_index(virt_addr_t va, uint32_t level) {
    return (uint32_t) ((va >> pte_level_shift(level)) & 0x1FFu);
}

static inline bool pte_level_supports_large(uint32_t level) {
    return level == 2u || level == 3u;
}

static inline uint32_t pte_page_size_to_level(page_size_t ps) {
    if(ps == ARCH_PAGE_SIZE_2M) return 2u;
    if(ps == ARCH_PAGE_SIZE_1G) return 3u;
    return 1u;
}

static inline bool pte_is_present(arch_pte_entry_t e) {
    return (e & ARCH_PTE_PRESENT) != 0u;
}

static inline bool pte_is_large(arch_pte_entry_t e, uint32_t level) {
    return level > 1u && (e & ARCH_PTE_LARGE) != 0u;
}

static inline phys_addr_t pte_get_table_phys(arch_pte_entry_t e) {
    return e & ARCH_PTE_ADDR_MASK;
}

static inline phys_addr_t pte_get_leaf_phys(arch_pte_entry_t e, uint32_t level) {
    return e & pte_addr_mask(level);
}

static inline arch_pte_entry_t pte_make_new_table(phys_addr_t phys, vm_protection_t prot, vm_privilege_t priv) {
    arch_pte_entry_t e = ARCH_PTE_PRESENT | (phys & ARCH_PTE_ADDR_MASK) | pte_priv_flags(priv);
    if(!prot.execute) e |= ARCH_PTE_NX;
    return e;
}

static inline arch_pte_entry_t pte_widen_table_entry(arch_pte_entry_t e, vm_protection_t prot, vm_privilege_t priv) {
    if(prot.write) e |= ARCH_PTE_RW;
    if(prot.execute) e &= ~ARCH_PTE_NX;
    e |= pte_priv_flags(priv);
    return e;
}

static inline arch_pte_entry_t pte_make_leaf_entry(phys_addr_t phys, vm_protection_t prot, vm_cache_t cache, vm_privilege_t priv, uint32_t level, bool global) {
    arch_pte_entry_t e = ARCH_PTE_PRESENT | (phys & pte_addr_mask(level)) | pte_priv_flags(priv) | pte_cache_flags(cache, level);
    if(level != 1u) e |= ARCH_PTE_LARGE;
    if(prot.write) e |= ARCH_PTE_RW;
    if(!prot.execute) e |= ARCH_PTE_NX;
    if(global) e |= ARCH_PTE_GLOBAL;
    return e;
}

static inline arch_pte_entry_t pte_rewrite_leaf_entry(arch_pte_entry_t old, vm_protection_t prot, vm_cache_t cache, vm_privilege_t priv, uint32_t level, bool global) {
    arch_pte_entry_t e = (old & pte_addr_mask(level)) | ARCH_PTE_PRESENT | (old & (ARCH_PTE_ACCESSED | ARCH_PTE_DIRTY));
    if(level != 1u) e |= ARCH_PTE_LARGE;
    e |= pte_priv_flags(priv);
    e |= pte_cache_flags(cache, level);
    if(prot.write) e |= ARCH_PTE_RW;
    if(!prot.execute) e |= ARCH_PTE_NX;
    if(global) e |= ARCH_PTE_GLOBAL;
    return e;
}

static inline arch_pte_entry_t pte_make_large_child(arch_pte_entry_t parent, phys_addr_t child_phys, uint32_t child_level) {
    uint64_t flags = parent & (ARCH_PTE_PRESENT | ARCH_PTE_RW | ARCH_PTE_USER | ARCH_PTE_WRITETHROUGH | ARCH_PTE_DISABLECACHE | ARCH_PTE_ACCESSED | ARCH_PTE_GLOBAL | ARCH_PTE_NX);
    arch_pte_entry_t e = flags | (child_phys & pte_addr_mask(child_level));
    if(child_level > 1u) e |= ARCH_PTE_LARGE;

    uint32_t parent_level = child_level + 1u;
    if(parent & pte_pat_bit(parent_level)) e |= pte_pat_bit(child_level);
    return e;
}

static inline arch_pte_entry_t pte_make_table_from_large(arch_pte_entry_t old_large, phys_addr_t new_table_phys) {
    return (old_large & (ARCH_PTE_PRESENT | ARCH_PTE_RW | ARCH_PTE_USER | ARCH_PTE_NX)) | (new_table_phys & ARCH_PTE_ADDR_MASK);
}
