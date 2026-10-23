#include <common/assert.h>
#include <common/cpu_local.h>
#include <common/interrupts/interrupt.h>
#include <common/log.h>
#include <common/sched/sched.h>
#include <common/sched/thread.h>
#include <common/userspace/signal.h>
#include <common/userspace/syscall.h>
#include <common/userspace/syscall_defs.h>
#include <common/userspace/userptr.h>
#include <common/userspace/userspace.h>
#include <lib/helpers.h>
#include <lib/string.h>
#include <memory/heap.h>
#include <memory/vm.h>
#include <uapi/signal.h>
#include <uapi/types.h>

#include "common/sched/process.h"

syscall_ret_t syscall_sys_proc_sig_action(syscall_args_t* args) {
    int signal_number = args->arg1;
    uintptr_t new_action_ptr = args->arg2; // sigaction*
    uintptr_t old_action_ptr = args->arg3; // sigaction*

    LOG_UTRC("signum=%d\n", signal_number);

    if(signal_number <= 0 || signal_number > UAPI_SIGNAL_MAX_SIGNAL) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);
    }

    if(uapi_sig_uncatchable(signal_number)) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);
    }

    process_t* process = sched_arch_thread_current()->process;

    uapi_signal_handler_t new_action;
    uapi_signal_handler_t old_action;
    if(new_action_ptr != 0) {
        vm_copy_from(&new_action, process->address_space, new_action_ptr, sizeof(uapi_signal_handler_t));
    }

    spinlock_lock(&process->signal_handler.lock);

    if(old_action_ptr != 0) {
        memory_copy(&old_action, &process->signal_handler.handlers[signal_number], sizeof(uapi_signal_handler_t));
    }

    if(new_action_ptr != 0) {
        memory_copy(&process->signal_handler.handlers[signal_number], &new_action, sizeof(uapi_signal_handler_t));
    }

    spinlock_unlock(&process->signal_handler.lock);

    if(old_action_ptr != 0) {
        vm_copy_to(process->address_space, old_action_ptr, &old_action, sizeof(uapi_signal_handler_t));
    }

    return SYSCALL_RET_VALUE(0);
}

syscall_ret_t syscall_sys_proc_sig_mask(syscall_args_t* args) {
    int how = args->arg1;
    uintptr_t set_ptr = args->arg2; // sigset_t*
    uintptr_t retrieve_ptr = args->arg3; // sigset_t*

    thread_t* thread = sched_arch_thread_current();

    uapi_signal_set_t new_set = 0;

    if(set_ptr != 0) {
        if(how != UAPI_SIGNAL_HOW_BLOCK && how != UAPI_SIGNAL_HOW_UNBLOCK && how != UAPI_SIGNAL_HOW_SETMASK) {
            return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);
        }
        vm_copy_from(&new_set, thread->process->address_space, set_ptr, sizeof(uapi_signal_set_t));
        new_set = signal_mask_sanitize(new_set);
    }

    spinlock_lock(&thread->signal.lock);
    uapi_signal_set_t old_set = thread->signal.masked_signal_set;

    if(set_ptr != 0) {
        switch(how) {
            case UAPI_SIGNAL_HOW_BLOCK:   thread->signal.masked_signal_set |= new_set; break;
            case UAPI_SIGNAL_HOW_UNBLOCK: thread->signal.masked_signal_set &= ~new_set; break;
            case UAPI_SIGNAL_HOW_SETMASK: thread->signal.masked_signal_set = new_set; break;
        }
        LOG_UTRC("old=0x%lx, new=0x%lx\n", new_set, thread->signal.masked_signal_set);
    }

    spinlock_unlock(&thread->signal.lock);

    if(retrieve_ptr != 0) {
        vm_copy_to(thread->process->address_space, retrieve_ptr, &old_set, sizeof(uapi_signal_set_t));
    }
    return SYSCALL_RET_VALUE(0);
}

syscall_ret_t syscall_sys_proc_sig_send_simple(syscall_args_t* args) {
    uapi_pid_t pid = args->arg1;
    uint32_t tid = args->arg2;
    int signal = args->arg3;

    if(pid <= 0) {
        /// @todo: process group signals
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);
    }

    if(signal < 0 || signal > UAPI_SIGNAL_MAX_SIGNAL) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);
    }

    process_t* target_process = process_find_by_id(pid);
    if(target_process == nullptr) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_SEARCH);
    }

    spinlock_nodw_lock(&target_process->status.lock);
    if(target_process->status.state != PROCESS_RUNNING) {
        spinlock_nodw_unlock(&target_process->status.lock);
        process_deref(target_process);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_SEARCH);
    }
    spinlock_nodw_unlock(&target_process->status.lock);

    // @todo: permission checks

    if(signal == 0) {
        process_deref(target_process);
        return SYSCALL_RET_VALUE(0);
    }

    uapi_signal_info_t info = { .code = UAPI_SIGNAL_USER, .signal_number = signal, .pid = sched_arch_thread_current()->process->process_id };

    /// tid == UINT32_MAX targets the whole process, otherwise a single thread
    if(tid == UINT32_MAX) {
        signal_process(target_process, signal, &info);
    } else {
        thread_t* target_thread = process_find_thread_by_id(target_process, tid);
        if(target_thread == nullptr) {
            process_deref(target_process);
            return SYSCALL_RET_ERROR(SYSCALL_ERROR_SEARCH);
        }

        signal_thread(target_thread, signal, &info);
        thread_deref(target_thread);
    }

    process_deref(target_process);
    return SYSCALL_RET_VALUE(0);
}

syscall_ret_t syscall_sys_proc_sig_send_alt_stack(syscall_args_t* args);

syscall_ret_t syscall_sys_proc_sig_send_return(syscall_args_t* args) {
    LOG_UTRC("returning from signal handler\n");

    thread_t* thread = sched_arch_thread_current();

    /// the restorer is reached by returning out of the signal handler, so the user stack pointer points directly at the ucontext that signal_arch_setup_frame pushed.
    userptr_t* ucontext_pointer = userptr_create(args->user_rsp, sizeof(uapi_signal_ucontext_t));
    if(ucontext_pointer == nullptr) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_FAULT);
    }

    uapi_signal_ucontext_t context;
    bool ok = userptr_read_full(ucontext_pointer, &context);
    heap_free(ucontext_pointer, sizeof(userptr_t));

    if(!ok) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_FAULT);
    }

    /// restore the signal mask that was in place before the handler ran
    spinlock_lock(&thread->signal.lock);
    thread->signal.masked_signal_set = signal_mask_sanitize(context.sigmask);
    spinlock_unlock(&thread->signal.lock);

    /// restores the interrupted context and jumps back to it
    signal_arch_restore_frame(&context);
}
