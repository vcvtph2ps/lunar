#include <common/assert.h>
#include <common/cpu_local.h>
#include <common/interrupts/interrupt.h>
#include <common/log.h>
#include <common/sched/sched.h>
#include <common/sched/thread.h>
#include <common/userspace/syscall.h>
#include <common/userspace/syscall_defs.h>
#include <common/userspace/userspace.h>
#include <memory/heap.h>

#include "arch/x86_64/cpu_local.h"
#include "arch/x86_64/hardware/fpu.h"
#include "arch/x86_64/sched/thread.h"
#include "lib/helpers.h"
#include "lib/string.h"

[[noreturn]] syscall_ret_t syscall_sys_proc_exit(syscall_args_t* args) {
    LOG_UTRC("exit_code=%d\n", (int) args->arg1);

    thread_t* thread = &CPU_LOCAL_GET_CURRENT_THREAD()->common;
    process_t* process = thread->process;

    ATOMIC_STORE(&thread->exit_code, (int32_t) args->arg1, ATOMIC_RELAXED);
    ATOMIC_STORE(&process->exit_code, (int32_t) args->arg1, ATOMIC_RELAXED);

    /// Exiting from a thread is exiting from the process, the kernel has no notion of a
    /// process outliving its last thread
    process_kill(process);

    /// process_kill never returns for a thread of the process it kills
    while(1);
}

#define SYSCALL_PROC_GETINFO_GROUP_ID 0
#define SYSCALL_PROC_GETINFO_EGROUP_ID 1
#define SYSCALL_PROC_GETINFO_USER_ID 2
#define SYSCALL_PROC_GETINFO_EUSER_ID 3
#define SYSCALL_PROC_GETINFO_PROCESS_ID 4
#define SYSCALL_PROC_GETINFO_PARENT_PROCESS 5
#define SYSCALL_PROC_GETINFO_THREAD_ID 6

syscall_ret_t syscall_sys_proc_getinfo(syscall_args_t* args) {
    process_t* current_process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;
    switch(args->arg1) {
        case SYSCALL_PROC_GETINFO_GROUP_ID:       LOG_UTRC("SYSCALL_PROC_GETINFO_GROUP_ID unimplemented!\n"); return SYSCALL_RET_VALUE(0);
        case SYSCALL_PROC_GETINFO_EGROUP_ID:      LOG_UTRC("SYSCALL_PROC_GETINFO_EGROUP_ID unimplemented!\n"); return SYSCALL_RET_VALUE(0);
        case SYSCALL_PROC_GETINFO_USER_ID:        LOG_UTRC("SYSCALL_PROC_GETINFO_USER_ID unimplemented!\n"); return SYSCALL_RET_VALUE(0);
        case SYSCALL_PROC_GETINFO_EUSER_ID:       LOG_UTRC("SYSCALL_PROC_GETINFO_EUSER_ID unimplemented!\n"); return SYSCALL_RET_VALUE(0);
        case SYSCALL_PROC_GETINFO_PROCESS_ID:     return SYSCALL_RET_VALUE(current_process->process_id);
        case SYSCALL_PROC_GETINFO_PARENT_PROCESS: return SYSCALL_RET_VALUE(current_process->parent_process_id);
        case SYSCALL_PROC_GETINFO_THREAD_ID:      return SYSCALL_RET_VALUE(CPU_LOCAL_GET_CURRENT_THREAD()->common.tid);
        default:                                  user_assert(false && "unreachable"); break;
    }

    ASSERT_UNREACHABLE();
}

syscall_ret_t syscall_sys_proc_fork(syscall_args_t* args) {
    LOG_UTRC("process forked\n");
    arch_interrupt_regs_t* frame = args->regs;

    uintptr_t entry = frame->rcx;

    process_t* new_process = process_fork(sched_arch_thread_current()->process);
    thread_t* thread = sched_arch_fork_thread_user(new_process, sched_arch_thread_current(), args->user_rsp, entry, frame);

    list_push(&new_process->thread_list, &thread->list_node_process);
    sched_thread_schedule(thread);

    return SYSCALL_RET_VALUE(new_process->process_id);
}
