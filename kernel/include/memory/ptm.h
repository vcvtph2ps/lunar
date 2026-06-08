#pragma once
#include <common/init.h>
#include <lib/types.h>
#include <memory/vm.h>

#define PTM_TO_HHDM(x) ((void*) ((uintptr_t) (x) + g_init_boot_info->hhdm_offset))
#define PTM_FROM_HHDM(x) ((void*) ((uintptr_t) (x) - g_init_boot_info->hhdm_offset))

/**
 * @brief Initializes the paging and virtual memory system for the current core
 * @param core_id The ID of the current core
 */
void ptm_init_kernel(uint32_t core_id);

/**
 * @brief Initializes the paging system for a user address space.
 * @param address_space The user address space to initialize.
 * @return true if the initialization was successful, false if it failed (e.g. out of memory)
 */
bool ptm_init_user(vm_address_space_t* address_space);

/**
 * @brief Maps a range of virtual addresses to physical addresses in the specified address space.
 * @param address_space The address space to modify.
 * @param vaddr The starting virtual address to map. Must be page-aligned.
 * @param paddr The starting physical address to map to. Must be page-aligned.
 * @param length The length of the mapping in bytes. Must be a multiple of the page size.
 * @param prot The memory protection flags for the mapping
 * @param cache The caching behavior for the mapping
 * @param privilege The privilege level required to access the mapping
 * @param global Whether the mapping should flushed on context switch or not (false means it will be flushed, true means it will not be flushed)
 * @return true if the mapping was successful, false if the mapping failed (e.g. out of memory)
 */
bool ptm_map(vm_address_space_t* address_space, virt_addr_t vaddr, phys_addr_t paddr, size_t length, vm_protection_t prot, vm_cache_t cache, vm_privilege_t privilege, bool global);

/**
 * @brief Changes the properties of an existing mapping in the specified address space.
 * @param address_space The address space to modify.
 * @param vaddr The starting virtual address of the mapping to modify. Must be page-aligned.
 * @param length The length of the mapping to modify in bytes. Must be a multiple of the page size.
 * @param prot The new memory protection flags for the mapping
 * @param cache The new caching behavior for the mapping
 * @param privilege The new privilege level required to access the mapping
 * @param global Whether the mapping should flushed on context switch or not (false means it will be flushed, true means it will not be flushed)
 * @return true if the rewrite was successful, false if the rewrite failed (e.g. out of memory)
 */
bool ptm_rewrite(vm_address_space_t* address_space, uintptr_t vaddr, size_t length, vm_protection_t prot, vm_cache_t cache, vm_privilege_t privilege, bool global);

/**
 * @brief Unmaps a range of virtual addresses in the specified address space.
 * @param address_space The address space to modify.
 * @param vaddr The starting virtual address to unmap. Must be page-aligned.
 * @param length The length of the mapping to unmap in bytes. Must be a multiple of the page size.
 */
void ptm_unmap(vm_address_space_t* address_space, uintptr_t vaddr, size_t length);

/**
 * @brief Translates a virtual address to a physical address in the specified address space.
 * @param address_space The address space to query.
 * @param vaddr The virtual address to translate.
 * @param paddr Output parameter to receive the translated physical address.
 * @return true if the translation was successful, false if the virtual address is not mapped.
 */
bool ptm_physical(vm_address_space_t* address_space, uintptr_t vaddr, uintptr_t* paddr);

/**
 * @brief Flushes the TLB entries for a range of virtual addresses in the specified address space.
 * @note This does not dispatch TLB flushes across cores
 * @param vaddr The starting virtual address to flush. Must be page-aligned.
 * @param length The length of the range to flush in bytes. Must be a multiple of the page size.
 */
void ptm_flush_tlb(virt_addr_t vaddr, size_t length);

/**
 * @brief Loads the page tables for the specified address space into the CPU's mmu.
 * @param address_space The address space to load.
 */
void ptm_load_address_space(vm_address_space_t* address_space);
