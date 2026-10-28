#include <common/cpu_local.h>
#include <common/log.h>
#include <common/sched/sched.h>
#include <common/sched/thread.h>
#include <common/userspace/syscall.h>
#include <common/userspace/syscall_defs.h>
#include <memory/heap.h>

#include "common/userspace/userptr.h"
#include "uapi/limits.h"

SYSCALL_DEFINE(syscall_sys_debug_log, SYSCALL_SYS_DEBUG_LOG, void, void*, string);
static syscall_ret_t syscall_sys_debug_log(syscall_args_t* argument_data, void* string) {
    (void) argument_data;
    userptr_t* ubuffer = userptr_create_string((uintptr_t) string, UAPI_LIMITS_ARG_MAX - 1);
    if(!ubuffer) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_TOO_BIG);
    }

    size_t buffer_size = ubuffer->size + 1;
    char* buffer = heap_alloc(buffer_size);
    if(buffer == nullptr) {
        heap_free(ubuffer, sizeof(userptr_t));
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_NOMEM);
    }

    if(ubuffer->size != 0 && !userptr_read_full(ubuffer, buffer)) {
        heap_free(buffer, buffer_size);
        heap_free(ubuffer, sizeof(userptr_t));
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_FAULT);
    }

    buffer[ubuffer->size] = '\0';
    heap_free(ubuffer, sizeof(userptr_t));

    LOG_DBGL("%s\n", buffer);
    heap_free(buffer, buffer_size);
    return SYSCALL_RET_VALUE(0);
}

#if defined(__x86_64__)
#include <arch/x86_64/internal/msr.h>

SYSCALL_DEFINE(syscall_sys_tcb_set, SYSCALL_SYS_TCB_SET, void, void*, fs_base);
static syscall_ret_t syscall_sys_tcb_set(syscall_args_t* argument_data, void* fs_base) {
    (void) argument_data;
    arch_msr_write(ARCH_MSR_FS_BASE, (uintptr_t) fs_base);
    CPU_LOCAL_GET_CURRENT_THREAD()->fsbase = (uintptr_t) fs_base;

    return SYSCALL_RET_VALUE(0);
}

#else

SYSCALL_DEFINE(syscall_sys_tcb_set, SYSCALL_SYS_TCB_SET, void, void*, fs_base);
static syscall_ret_t syscall_sys_tcb_set(syscall_args_t* argument_data, void* fs_base) {
    (void) argument_data;
    (void) fs_base;
    assert(false && "unimplemented");
}

#endif
