#include <arch/riscv64/internal/csr.h>
#include <common/arch.h>
#include <common/assert.h>
#include <common/init.h>
#include <common/log.h>
#include <common/memory.h>
#include <memory/pagedb.h>
#include <memory/pmm.h>
#include <memory/ptm.h>
#include <memory/vm.h>


[[noreturn]] void ptm_init_kernel(uint32_t core_id) {
    (void) core_id;
    vm_init_kernel();
    assert(false && "Not yet implemented");
}

bool ptm_init_user(vm_address_space_t* address_space) {
    (void) address_space;
    assert(false && "Not yet implemented");
    return false;
}

bool ptm_map(vm_address_space_t* address_space, virt_addr_t vaddr, phys_addr_t paddr, size_t length, vm_protection_t prot, vm_cache_t cache, vm_privilege_t privilege, bool global) {
    (void) address_space;
    (void) vaddr;
    (void) paddr;
    (void) length;
    (void) prot;
    (void) cache;
    (void) privilege;
    (void) global;
    assert(false && "Not yet implemented");
    return false;
}
bool ptm_rewrite(vm_address_space_t* address_space, uintptr_t vaddr, size_t length, vm_protection_t prot, vm_cache_t cache, vm_privilege_t privilege, bool global) {
    (void) address_space;
    (void) vaddr;
    (void) length;
    (void) prot;
    (void) cache;
    (void) privilege;
    (void) global;
    assert(false && "Not yet implemented");
    return false;
}

[[noreturn]] void ptm_unmap(vm_address_space_t* address_space, uintptr_t vaddr, size_t length) {
    (void) address_space;
    (void) vaddr;
    (void) length;
    assert(false && "Not yet implemented");
}

bool ptm_physical(vm_address_space_t* address_space, uintptr_t vaddr, uintptr_t* paddr) {
    (void) address_space;
    (void) vaddr;
    (void) paddr;
    assert(false && "Not yet implemented");
    return false;
}


[[noreturn]] void ptm_flush_tlb(virt_addr_t vaddr, size_t length) {
    (void) vaddr;
    (void) length;
    assert(false && "Not yet implemented");
}

[[noreturn]] void ptm_load_address_space(vm_address_space_t* address_space) {
    (void) address_space;
    assert(false && "Not yet implemented");
}
