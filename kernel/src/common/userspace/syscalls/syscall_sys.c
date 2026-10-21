#include <common/cpu_local.h>
#include <common/log.h>
#include <common/sched/sched.h>
#include <common/sched/thread.h>
#include <common/userspace/syscall.h>
#include <common/userspace/syscall_defs.h>
#include <memory/heap.h>

#include "common/userspace/userptr.h"
#include "uapi/limits.h"

syscall_ret_t syscall_sys_debug_log(syscall_args_t* args) {
    userptr_t* ubuffer = userptr_create_string(args->arg1, UAPI_LIMITS_PATH_MAX - 1);
    if(!ubuffer) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_TOO_BIG);
    }

    size_t buffer_size = ubuffer->size + 1;
    char* buffer = heap_alloc(buffer_size);
    if(buffer == nullptr) {
        heap_free(ubuffer, sizeof(userptr_t));
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_NOMEM);
    }

    if(ubuffer->size != 0 && !userptr_read(ubuffer, buffer)) {
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

#if defined(__ARCH_X86_64__)
#include <arch/x86_64/internal/msr.h>

syscall_ret_t syscall_sys_thread_set_tcb(syscall_args_t* args) {
    arch_msr_write(ARCH_MSR_FS_BASE, args->arg1);
    CPU_LOCAL_GET_CURRENT_THREAD()->fsbase = args->arg1;
    LOG_UTRC("tcb=0x%lx\n", args->arg1);

    return SYSCALL_RET_VALUE(0);
}
#else
syscall_ret_t syscall_sys_thread_set_tcb(syscall_args_t* args) {
    (void) args;
    assert(false && "unimplemented");
}
#endif
