#include <common/assert.h>
#include <common/cpu_local.h>
#include <common/interrupts/interrupt.h>
#include <common/log.h>
#include <common/sched/process.h>
#include <common/sched/sched.h>
#include <common/sched/thread.h>
#include <common/sync/spinlock.h>
#include <common/userspace/syscall.h>
#include <common/userspace/syscall_defs.h>
#include <common/userspace/userspace.h>
#include <lib/helpers.h>
#include <memory/heap.h>
#include <memory/vm.h>
#include <uapi/types.h>

[[noreturn]] syscall_ret_t syscall_sys_proc_exit(syscall_args_t* args) {
    LOG_UTRC("exit_code=%d\n", (int) args->arg1);

    thread_t* thread = &CPU_LOCAL_GET_CURRENT_THREAD()->common;
    process_t* process = thread->process;

    ATOMIC_STORE(&thread->exit_code, (int32_t) args->arg1, ATOMIC_RELAXED);

    process_kill(process, PROCESS_EXITED, (int32_t) args->arg1);
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

/// Return immediately if no child has exited yet
#define SYSCALL_WAITPID_NOHANG 1

syscall_ret_t syscall_sys_proc_waitpid(syscall_args_t* args) {
    int32_t pid = (int32_t) args->arg1;
    uintptr_t status_ptr = args->arg2; // int*
    int flags = (int) args->arg3;
    // uintptr_t ru = args->arg4; // struct rusage*, unimplemented

    LOG_UTRC("pid=%d, flags=%d\n", pid, flags);

    process_t* parent = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;

    if(pid == 0 || pid < -1) {
        user_assert("unimplemented");
    }

    while(true) {
        process_t* child = nullptr;

        spinlock_nodw_lock(&parent->children_lock);
        LIST_FOR_EACH(&parent->children_list, node) {
            process_t* candidate = CONTAINER_OF(node, process_t, list_node_sibling);

            if(pid > 0 && candidate->process_id != (uint32_t) pid) {
                continue;
            }

            spinlock_nodw_lock(&candidate->status.lock);
            if(candidate->status.state != PROCESS_RUNNING) {
                spinlock_nodw_unlock(&candidate->status.lock);
                list_node_delete(&parent->children_list, &candidate->list_node_sibling);
                child = candidate;
                break;
            }
            spinlock_nodw_unlock(&candidate->status.lock);
        }

        /// Determine whether any child matching the request is still alive so we know whether to wait or to fail with ECHILD
        bool child_exists = child != nullptr;
        if(!child_exists) {
            LIST_FOR_EACH(&parent->children_list, node) {
                process_t* candidate = CONTAINER_OF(node, process_t, list_node_sibling);
                if(pid == -1 || candidate->process_id == (uint32_t) pid) {
                    child_exists = true;
                    break;
                }
            }
        }
        spinlock_nodw_unlock(&parent->children_lock);

        if(child != nullptr) {
            uint32_t status = 0;

            spinlock_nodw_lock(&child->status.lock);

            switch(child->status.state) {
                case PROCESS_EXITED:     status = ((uint32_t) child->status.exited.exit_code & 0xffu) << 8; break;
                case PROCESS_TERMINATED: status = (uint32_t) child->status.terminated.signal_number & 0x7fu; break;
                default:                 ASSERT_UNREACHABLE();
            }

            child->status.state = PROCESS_PENDING_REAP;
            spinlock_nodw_unlock(&child->status.lock);

            if(status_ptr != 0) {
                if(vm_copy_to(parent->address_space, status_ptr, &status, sizeof(status)) != sizeof(status)) {
                    return SYSCALL_RET_ERROR(SYSCALL_ERROR_FAULT);
                }
            }
            return SYSCALL_RET_VALUE(child->process_id);
        }

        /// No child (left) matches the request
        if(!child_exists) {
            return SYSCALL_RET_ERROR(SYSCALL_ERROR_CHILD);
        }

        if(flags & SYSCALL_WAITPID_NOHANG) {
            return SYSCALL_RET_VALUE(0);
        }

        sched_wait_single(&parent->child_exit_event, UINT64_MAX);
    }
}
