#pragma once
#include <common/assert.h>
#include <common/memory.h>
#include <lib/types.h>
#include <memory/vm.h>
#include <stdbool.h>
#include <stdint.h>

typedef uint64_t arch_pte_entry_t;

#define ARCH_PTE_MAX_LEVELS 5
extern uint32_t g_arch_pte_top_level;
extern bool g_arch_pte_svpbmt_enabled;

#define ARCH_PTE_V ((uint64_t) (1u << 0))
#define ARCH_PTE_R ((uint64_t) (1u << 1))
#define ARCH_PTE_W ((uint64_t) (1u << 2))
#define ARCH_PTE_X ((uint64_t) (1u << 3))
#define ARCH_PTE_U ((uint64_t) (1u << 4))
#define ARCH_PTE_G ((uint64_t) (1u << 5))
#define ARCH_PTE_A ((uint64_t) (1u << 6))
#define ARCH_PTE_D ((uint64_t) (1u << 7))

#define ARCH_PTE_PBMT_PMA ((uint64_t) (0ULL << 61)) // Normal
#define ARCH_PTE_PBMT_NC ((uint64_t) (1ULL << 61)) // Non-cacheable, idempotent
#define ARCH_PTE_PBMT_IO ((uint64_t) (2ULL << 61)) // Non-cacheable, non-idempotent
#define ARCH_PTE_PBMT_MASK ((uint64_t) (3ULL << 61))

#define ARCH_PTE_PPN_MASK ((uint64_t) 0x003FFFFFFFFFFC00ULL)

#define ARCH_PTE_PHYS_TO_PTE(pa) (((uint64_t) (pa) >> 2u) & ARCH_PTE_PPN_MASK)
#define ARCH_PTE_TO_PHYS(pte) (((uint64_t) (pte) & ARCH_PTE_PPN_MASK) << 2u)

static inline uint32_t pte_top_level(void) {
    return g_arch_pte_top_level;
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
    return (e & ARCH_PTE_V) != 0u;
}

static inline bool pte_is_large(arch_pte_entry_t e, uint32_t level) {
    return level > 1u && (e & (ARCH_PTE_R | ARCH_PTE_W | ARCH_PTE_X)) != 0u;
}

static inline phys_addr_t pte_get_table_phys(arch_pte_entry_t e) {
    return ARCH_PTE_TO_PHYS(e);
}

static inline phys_addr_t pte_get_leaf_phys(arch_pte_entry_t e, uint32_t level) {
    phys_addr_t pa = ARCH_PTE_TO_PHYS(e);
    return pa & ~((phys_addr_t) (pte_level_page_size(level) - 1u));
}

static inline uint64_t pte_cache_flags(vm_cache_t cache, uint32_t level) {
    (void) level;
    if(!g_arch_pte_svpbmt_enabled) return 0u;
    switch(cache) {
        case VM_CACHE_NORMAL:        return ARCH_PTE_PBMT_PMA;
        case VM_CACHE_WRITE_THROUGH: return ARCH_PTE_PBMT_PMA;
        case VM_CACHE_DISABLE:       return ARCH_PTE_PBMT_IO;
        case VM_CACHE_WRITE_COMBINE: return ARCH_PTE_PBMT_NC;
    }
    __builtin_unreachable();
}

static inline uint64_t pte_priv_flags(vm_privilege_t priv) {
    return priv == VM_PRIVILEGE_USER ? ARCH_PTE_U : 0u;
}

static inline arch_pte_entry_t pte_make_new_table(phys_addr_t phys, vm_protection_t prot, vm_privilege_t priv) {
    (void) prot;
    (void) priv;
    // On riscv intermediate pt entries carry no protection or privilege bits.
    return ARCH_PTE_V | ARCH_PTE_PHYS_TO_PTE(phys);
}

static inline arch_pte_entry_t pte_widen_table_entry(arch_pte_entry_t e, vm_protection_t prot, vm_privilege_t priv) {
    (void) prot;
    (void) priv;
    // nothing to widen for intermediate pt entries
    return e;
}

static inline arch_pte_entry_t pte_make_leaf_entry(phys_addr_t phys, vm_protection_t prot, vm_cache_t cache, vm_privilege_t priv, uint32_t level, bool global) {
    arch_pte_entry_t e = ARCH_PTE_V | ARCH_PTE_PHYS_TO_PTE(phys & ~((phys_addr_t) (pte_level_page_size(level) - 1u)));
    if(prot.read) e |= ARCH_PTE_R;
    if(prot.write) e |= ARCH_PTE_W;
    if(prot.execute) e |= ARCH_PTE_X;
    e |= pte_priv_flags(priv);
    if(global) e |= ARCH_PTE_G;
    e |= pte_cache_flags(cache, level);
    return e;
}

static inline arch_pte_entry_t pte_rewrite_leaf_entry(arch_pte_entry_t old, vm_protection_t prot, vm_cache_t cache, vm_privilege_t priv, uint32_t level, bool global) {
    arch_pte_entry_t e = ARCH_PTE_V | (old & ARCH_PTE_PPN_MASK) | (old & (ARCH_PTE_A | ARCH_PTE_D));
    if(prot.read) e |= ARCH_PTE_R;
    if(prot.write) e |= ARCH_PTE_W;
    if(prot.execute) e |= ARCH_PTE_X;
    e |= pte_priv_flags(priv);
    if(global) e |= ARCH_PTE_G;
    e |= pte_cache_flags(cache, level);
    return e;
}

static inline arch_pte_entry_t pte_make_large_child(arch_pte_entry_t parent, phys_addr_t child_phys, uint32_t child_level) {
    (void) child_level;
    uint64_t flags = parent & (ARCH_PTE_V | ARCH_PTE_R | ARCH_PTE_W | ARCH_PTE_X | ARCH_PTE_U | ARCH_PTE_G | ARCH_PTE_A | ARCH_PTE_D | ARCH_PTE_PBMT_MASK);
    return flags | ARCH_PTE_PHYS_TO_PTE(child_phys);
}

static inline arch_pte_entry_t pte_make_table_from_large(arch_pte_entry_t old_large, phys_addr_t new_table_phys) {
    (void) old_large;
    return ARCH_PTE_V | ARCH_PTE_PHYS_TO_PTE(new_table_phys);
}
