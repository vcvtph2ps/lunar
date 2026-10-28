#include <common/log.h>
#include <common/userspace/syscall.h>
#include <common/userspace/syscall_defs.h>
#include <common/userspace/userspace.h>
#include <memory/vm.h>
#include <uapi/mmem.h>


SYSCALL_DEFINE(syscall_vm_map, SYSCALL_VM_MAP, void*, void*, hint, size_t, size, size_t, prot, size_t, flags, size_t, fd, size_t, offset);
static syscall_ret_t syscall_vm_map(syscall_args_t* argument_data, void* hint, size_t size, size_t prot, size_t flags, size_t fd, size_t offset) {
    (void) argument_data;
    (void) fd;
    (void) offset;

    // LOG_UTRC("hint=0x%016lx size=%zu prot=[%c%c%c] flags=0x%lx fd=%ld offset=0x%lx\n", hint, size, (prot & UAPI_PROT_READ) != 0 ? 'R' : '-', (prot & UAPI_PROT_WRITE) != 0 ? 'W' : '-', (prot & UAPI_PROT_EXEC) != 0 ? 'E' : '-', flags, fd, offset);
    user_assert((flags & (UAPI_MAP_ANON)) != 0 && "unimplemented");

    // @note: non readable mappings aren't supported
    vm_protection_t vm_prot = VM_PROT_RO;
    if(prot & UAPI_PROT_READ) {
        vm_prot.read = true;
    }
    if(prot & UAPI_PROT_WRITE) {
        vm_prot.write = true;
    }
    if(prot & UAPI_PROT_EXEC) {
        vm_prot.execute = true;
    }

    uint64_t vm_flags = VM_FLAG_NONE;
    if(flags & UAPI_MAP_FIXED) {
        vm_flags |= VM_FLAG_FIXED;
    }
    if(flags & UAPI_MAP_SHARED) {
        vm_flags |= VM_FLAG_SHARED;
    }
    if(flags & UAPI_MAP_ANON) {
        vm_flags |= VM_FLAG_ZERO;
    }

    process_t* current_process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;

    virt_addr_t vaddr = (virt_addr_t) vm_map_anon(current_process->address_space, (void*) hint, ALIGN_UP(size, PAGE_SIZE_DEFAULT), vm_prot, VM_CACHE_NORMAL, vm_flags);
    // LOG_UTRC(
    //     "hint=0x%016lx size=%zu prot=[%c%c%c] flags=0x%lx fd=%ld offset=0x%lx | result=0x%lx\n",
    //     hint,
    //     size,
    //     (prot & UAPI_PROT_READ) != 0 ? 'R' : '-',
    //     (prot & UAPI_PROT_WRITE) != 0 ? 'W' : '-',
    //     (prot & UAPI_PROT_EXEC) != 0 ? 'E' : '-',
    //     flags,
    //     fd,
    //     offset,
    //     vaddr
    // );
    if(vaddr == 0) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);
    }
    return SYSCALL_RET_VALUE(vaddr);
}

SYSCALL_DEFINE(syscall_vm_unmap, SYSCALL_VM_UNMAP, void, void*, addr, size_t, size);
static syscall_ret_t syscall_vm_unmap(syscall_args_t* argument_data, void* addr, size_t size) {
    (void) argument_data;
    process_t* current_process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;
    vm_unmap(current_process->address_space, addr, size);
    return SYSCALL_RET_VOID();
}

SYSCALL_DEFINE(syscall_vm_protect, SYSCALL_VM_PROTECT, void, void*, addr, size_t, size, size_t, prot);
syscall_ret_t syscall_vm_protect(syscall_args_t* argument_data, void* addr, size_t size, size_t prot) {
    (void) argument_data;
    // @note: non readable mappings aren't supported
    vm_protection_t vm_prot = VM_PROT_RO;
    if(prot & UAPI_PROT_READ) {
        vm_prot.read = true;
    }
    if(prot & UAPI_PROT_WRITE) {
        vm_prot.write = true;
    }
    if(prot & UAPI_PROT_EXEC) {
        vm_prot.execute = true;
    }

    process_t* current_process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;
    vm_rewrite_prot(current_process->address_space, (void*) addr, size, vm_prot);
    return SYSCALL_RET_VOID();
}
