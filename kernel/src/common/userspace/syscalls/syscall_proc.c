#include <common/assert.h>
#include <common/cpu_local.h>
#include <common/log.h>
#include <common/sched/sched.h>
#include <common/sched/thread.h>
#include <common/userspace/syscall.h>
#include <common/userspace/syscall_defs.h>
#include <common/userspace/userspace.h>
#include <memory/heap.h>

[[noreturn]] syscall_ret_t syscall_sys_proc_exit(syscall_args_t* args) {
    (void) args;
    LOG_UTRC("exit_code=%d\n", (int) args->arg1);
    process_kill(CPU_LOCAL_GET_CURRENT_THREAD()->common.process);
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
        case SYSCALL_PROC_GETINFO_PARENT_PROCESS: LOG_UTRC("SYSCALL_PROC_GETINFO_PARENT_PROCESS unimplemented!\n"); return SYSCALL_RET_VALUE(0);
        case SYSCALL_PROC_GETINFO_THREAD_ID:      return SYSCALL_RET_VALUE(CPU_LOCAL_GET_CURRENT_THREAD()->common.tid);
        default:                                  user_assert(false && "unreachable"); break;
    }

    ASSERT_UNREACHABLE();
}
