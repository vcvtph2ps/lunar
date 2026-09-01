#include <arch/x86_64/internal/msr.h>
#include <common/log.h>
#include <common/userspace/syscall.h>
#include <common/userspace/syscall_defs.h>
#include <common/userspace/userspace.h>

#include "memory/vm.h"

#define PROT_NONE (0)
#define PROT_READ (1 << 0)
#define PROT_WRITE (1 << 1)
#define PROT_EXEC (1 << 2)

#define MAP_FILE (0)
#define MAP_SHARED (1 << 0)
#define MAP_PRIVATE (1 << 1)
#define MAP_FIXED (1 << 4)
#define MAP_ANON (1 << 5)

syscall_ret_t syscall_sys_vm_map(syscall_args_t* args) {
    uintptr_t hint = args->arg1;
    size_t size = args->arg2;
    size_t prot = args->arg3;
    size_t flags = args->arg4;
    size_t fd = args->arg5;
    size_t offset = args->arg6;
    (void) fd;
    (void) offset;

    LOG_STRC("hint=0x%016lx size=%zu prot=[%c%c%c] flags=0x%lx fd=%ld offset=0x%lx\n", hint, size, (prot & PROT_READ) != 0 ? 'R' : '-', (prot & PROT_WRITE) != 0 ? 'W' : '-', (prot & PROT_EXEC) != 0 ? 'E' : '-', flags, fd, offset);
    user_assert((flags & (MAP_ANON)) != 0 && "unimplemented");

    vm_protection_t vm_prot = VM_PROT_RO;
    // @note: non readable mappings aren't supported
    // if(prot & PROT_READ) {
    //     vm_prot.read = true;
    // }
    if(prot & PROT_WRITE) {
        vm_prot.write = true;
    }
    if(prot & PROT_EXEC) {
        vm_prot.execute = true;
    }

    uint64_t vm_flags = VM_FLAG_NONE;
    if(flags & MAP_FIXED) {
        vm_flags |= VM_FLAG_FIXED;
    }

    if(flags & MAP_SHARED) {
        vm_flags |= VM_FLAG_SHARED;
    }

    if(flags & MAP_ANON) {
        vm_flags |= VM_FLAG_ZERO;
    }

    process_t* current_process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;

    virt_addr_t vaddr = (virt_addr_t) vm_map_anon(current_process->address_space, (void*) hint, ALIGN_UP(size, PAGE_SIZE_DEFAULT), vm_prot, VM_CACHE_NORMAL, vm_flags);
    if(vaddr == 0) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);
    }
    return SYSCALL_RET_VALUE(vaddr);
}

syscall_ret_t syscall_sys_vm_unmap(syscall_args_t* args) {
    void* addr = (void*) args->arg1;
    size_t size = args->arg2;

    process_t* current_process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;
    vm_unmap(current_process->address_space, addr, size);
    return SYSCALL_RET_VALUE(0);
}

syscall_ret_t syscall_sys_vm_protect(syscall_args_t* args) {
    uintptr_t addr = (uintptr_t) args->arg1;
    size_t size = args->arg2;
    size_t prot = args->arg3;

    vm_protection_t vm_prot = VM_PROT_RO;
    if(prot & PROT_READ) {
        vm_prot.read = true;
    }
    if(prot & PROT_WRITE) {
        vm_prot.write = true;
    }
    if(prot & PROT_EXEC) {
        vm_prot.execute = true;
    }

    process_t* current_process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;

    LOG_STRC("addr=0x%016lx size=%zu prot=%zu\n", addr, size, prot);

    vm_rewrite_prot(current_process->address_space, (void*) addr, size, vm_prot);
    return SYSCALL_RET_VALUE(0);
}
