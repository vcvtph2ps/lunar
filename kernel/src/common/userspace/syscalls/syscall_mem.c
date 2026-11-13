#include <common/fs/vfs.h>
#include <common/fs/vfs_inode.h>
#include <common/log.h>
#include <common/memory.h>
#include <common/userspace/fd_store.h>
#include <common/userspace/syscall.h>
#include <common/userspace/syscall_defs.h>
#include <common/userspace/userspace.h>
#include <memory/page_cache.h>
#include <memory/vm.h>
#include <stdint.h>
#include <uapi/mmem.h>


static void log_prot(size_t prot) {
    log_print_lockless(LOG_LEVEL_STRC, "[%c%c%c]", (prot & UAPI_PROT_READ) != 0 ? 'R' : '-', (prot & UAPI_PROT_WRITE) != 0 ? 'W' : '-', (prot & UAPI_PROT_EXEC) != 0 ? 'E' : '-');
}

static void log_map_flags(size_t flags) {
    bool anon = (flags & UAPI_MAP_ANON) != 0;
    log_print_lockless(LOG_LEVEL_STRC, "%s", anon ? "ANON" : "FILE");

    static const struct {
        size_t bit;
        const char* name;
    } map_flags[] = {
        { UAPI_MAP_SHARED,  "SHARED"  },
        { UAPI_MAP_PRIVATE, "PRIVATE" },
        { UAPI_MAP_FIXED,   "FIXED"   },
    };

    size_t remaining = flags & ~UAPI_MAP_ANON;

    for(size_t i = 0; i < sizeof(map_flags) / sizeof(map_flags[0]); i++) {
        if((flags & map_flags[i].bit) == 0) {
            continue;
        }
        log_print_lockless(LOG_LEVEL_STRC, "|%s", map_flags[i].name);
        remaining &= ~map_flags[i].bit;
    }

    if(remaining != 0) {
        log_print_lockless(LOG_LEVEL_STRC, "|unknown=0x%lx", (unsigned long) remaining);
    }
}

SYSCALL_DEFINE_TRACED(syscall_vm_map, SYSCALL_VM_MAP, void*, syscall_vm_map_trace, void*, hint, size_t, size, size_t, prot, size_t, flags, size_t, fd, size_t, offset);

static void syscall_vm_map_trace(syscall_args_t* argument_data, void* hint, size_t size, size_t prot, size_t flags, size_t fd, size_t offset) {
    (void) argument_data;

    log_print_lockless(LOG_LEVEL_STRC, "hint=0x%016lx size=%zu prot=", (unsigned long) (uintptr_t) hint, size);
    log_prot(prot);
    log_print_lockless(LOG_LEVEL_STRC, " flags=[");
    log_map_flags(flags);
    log_print_lockless(LOG_LEVEL_STRC, "] fd=%ld offset=0x%lx\n", (long) fd, (unsigned long) offset);
}

static syscall_ret_t syscall_vm_map(syscall_args_t* argument_data, void* hint, size_t size, size_t prot, size_t flags, size_t fd, size_t offset) {
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

    uint64_t vm_flags = VM_FLAG_NONE;
    if(flags & UAPI_MAP_FIXED) {
        vm_flags |= VM_FLAG_FIXED;
    }
    if(flags & UAPI_MAP_SHARED) {
        vm_flags |= VM_FLAG_SHARED;
    }

    process_t* current_process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;
    size_t aligned_size = ALIGN_UP(size, PAGE_SIZE_DEFAULT);

    virt_addr_t vaddr;
    if(flags & UAPI_MAP_ANON) {
        vm_flags |= VM_FLAG_ZERO;
        vaddr = (virt_addr_t) vm_map_anon(current_process->address_space, (void*) hint, aligned_size, vm_prot, VM_CACHE_NORMAL, vm_flags);
    } else {
        if(fd == (size_t) -1)
            return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);

        fd_store_entry_t* entry = fd_store_get_fd(current_process->fd_store, (uint32_t) fd);
        if(entry == nullptr)
            return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADF);

        vfs_inode_t* inode = vfs_node_get_inode(entry->node);
        if(inode == nullptr)
            return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);
        if(offset % PAGE_SIZE_DEFAULT != 0 || offset >= inode->size)
            return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);


        vaddr = (virt_addr_t) vm_map_file(current_process->address_space, (void*) hint, aligned_size, vm_prot, VM_CACHE_NORMAL, vm_flags, vfs_inode_cache(inode), offset);
    }

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

SYSCALL_DEFINE_TRACED(syscall_vm_protect, SYSCALL_VM_PROTECT, void, syscall_vm_protect_trace, void*, addr, size_t, size, size_t, prot);

static void syscall_vm_protect_trace(syscall_args_t* argument_data, void* addr, size_t size, size_t prot) {
    (void) argument_data;

    log_print_lockless(LOG_LEVEL_STRC, "addr=0x%lx size=%zu prot=", (unsigned long) (uintptr_t) addr, size);
    log_prot(prot);
    log_print_lockless(LOG_LEVEL_STRC, "\n");
}

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
