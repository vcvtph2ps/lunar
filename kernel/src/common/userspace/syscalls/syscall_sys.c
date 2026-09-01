#include <arch/x86_64/internal/msr.h>
#include <common/cpu_local.h>
#include <common/log.h>
#include <common/sched/sched.h>
#include <common/sched/thread.h>
#include <common/userspace/syscall.h>
#include <common/userspace/syscall_defs.h>
#include <memory/heap.h>

[[noreturn]] syscall_ret_t syscall_sys_proc_exit(syscall_args_t* args) {
    (void) args;
    LOG_STRC("exit_code=%d\n", (int) args->arg1);
    sched_yield(THREAD_STATE_DEAD);
    while(1);
}

syscall_ret_t syscall_sys_debug_log(syscall_args_t* args) {
    uintptr_t ubuffer = args->arg1;
    size_t ubuffer_size = args->arg2;

    char* message = heap_alloc(ubuffer_size);
    vm_copy_from(message, CPU_LOCAL_GET_CURRENT_THREAD()->common.process->address_space, ubuffer, ubuffer_size);

    LOG_DBGL("%.*s\n", (int) ubuffer_size, message);

    heap_free(message, ubuffer_size);
    return SYSCALL_RET_VALUE(0);
}

syscall_ret_t syscall_sys_thread_set_tcb(syscall_args_t* args) {
    arch_msr_write(ARCH_MSR_FS_BASE, args->arg1);
    LOG_STRC("tcb=0x%lx\n", args->arg1);

    return SYSCALL_RET_VALUE(0);
}
