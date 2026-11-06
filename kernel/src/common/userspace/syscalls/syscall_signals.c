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

static const char* signal_name(int signal_number) {
    switch(signal_number) {
        case UAPI_SIGNAL_HUP:    return "SIGHUP";
        case UAPI_SIGNAL_INT:    return "SIGINT";
        case UAPI_SIGNAL_QUIT:   return "SIGQUIT";
        case UAPI_SIGNAL_CONT:   return "SIGCONT";
        case UAPI_SIGNAL_BUS:    return "SIGBUS";
        case UAPI_SIGNAL_ABRT:   return "SIGABRT";
        case UAPI_SIGNAL_CHLD:   return "SIGCHLD";
        case UAPI_SIGNAL_FPE:    return "SIGFPE";
        case UAPI_SIGNAL_KILL:   return "SIGKILL";
        case UAPI_SIGNAL_ILL:    return "SIGILL";
        case UAPI_SIGNAL_PIPE:   return "SIGPIPE";
        case UAPI_SIGNAL_SEGV:   return "SIGSEGV";
        case UAPI_SIGNAL_STOP:   return "SIGSTOP";
        case UAPI_SIGNAL_ALRM:   return "SIGALRM";
        case UAPI_SIGNAL_TERM:   return "SIGTERM";
        case UAPI_SIGNAL_TSTP:   return "SIGTSTP";
        case UAPI_SIGNAL_TTIN:   return "SIGTTIN";
        case UAPI_SIGNAL_TTOU:   return "SIGTTOU";
        case UAPI_SIGNAL_USR1:   return "SIGUSR1";
        case UAPI_SIGNAL_USR2:   return "SIGUSR2";
        case UAPI_SIGNAL_IO:     return "SIGIO";
        case UAPI_SIGNAL_PROF:   return "SIGPROF";
        case UAPI_SIGNAL_SYS:    return "SIGSYS";
        case UAPI_SIGNAL_TRAP:   return "SIGTRAP";
        case UAPI_SIGNAL_URG:    return "SIGURG";
        case UAPI_SIGNAL_VTALRM: return "SIGVTALRM";
        case UAPI_SIGNAL_XCPU:   return "SIGXCPU";
        case UAPI_SIGNAL_XFSZ:   return "SIGXFSZ";
        case UAPI_SIGNAL_WINCH:  return "SIGWINCH";
        case UAPI_SIGNAL_PWR:    return "SIGPWR";
        case UAPI_SIGNAL_CANCEL: return "SIGCANCEL";
        default:                 return nullptr;
    }
}

static void log_signal_number(int signal_number) {
    const char* name = signal_name(signal_number);
    if(name != nullptr) {
        log_print_lockless(LOG_LEVEL_STRC, "%s(%d)", name, signal_number);
    } else {
        log_print_lockless(LOG_LEVEL_STRC, "%d", signal_number);
    }
}

static const char* signal_how_name(int how) {
    switch(how) {
        case UAPI_SIGNAL_HOW_BLOCK:   return "BLOCK";
        case UAPI_SIGNAL_HOW_UNBLOCK: return "UNBLOCK";
        case UAPI_SIGNAL_HOW_SETMASK: return "SETMASK";
        default:                      return nullptr;
    }
}

SYSCALL_DEFINE_TRACED(syscall_proc_sig_action, SYSCALL_PROC_SIG_ACTION, void, syscall_proc_sig_action_trace, int32_t, signal_number, void*, new_action, void*, old_action);

static void syscall_proc_sig_action_trace(syscall_args_t* argument_data, int32_t signal_number, void* new_action, void* old_action) {
    (void) argument_data;

    log_print_lockless(LOG_LEVEL_STRC, "signal=");
    log_signal_number(signal_number);
    log_print_lockless(
        LOG_LEVEL_STRC, ", new_action=0x%lx, old_action=0x%lx\n", (unsigned long) (uintptr_t) new_action, (unsigned long) (uintptr_t) old_action
    );
}

static syscall_ret_t syscall_proc_sig_action(syscall_args_t* argument_data, int32_t signal_number, void* new_action_ptr, void* old_action_ptr) {
    (void) argument_data;

    if(signal_number <= 0 || signal_number > UAPI_SIGNAL_MAX_SIGNAL) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);
    }

    if(uapi_sig_uncatchable(signal_number)) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);
    }

    process_t* process = sched_arch_thread_current()->process;

    uapi_signal_handler_t new_action;
    uapi_signal_handler_t old_action;
    if(new_action_ptr != nullptr) {
        vm_copy_from(&new_action, process->address_space, (uintptr_t) new_action_ptr, sizeof(uapi_signal_handler_t));
    }

    spinlock_lock(&process->signal_handler.lock);

    if(old_action_ptr != nullptr) {
        memory_copy(&old_action, &process->signal_handler.handlers[signal_number], sizeof(uapi_signal_handler_t));
    }

    if(new_action_ptr != nullptr) {
        memory_copy(&process->signal_handler.handlers[signal_number], &new_action, sizeof(uapi_signal_handler_t));
    }

    spinlock_unlock(&process->signal_handler.lock);

    if(old_action_ptr != nullptr) {
        vm_copy_to(process->address_space, (uintptr_t) old_action_ptr, &old_action, sizeof(uapi_signal_handler_t));
    }

    return SYSCALL_RET_VALUE(0);
}

SYSCALL_DEFINE_TRACED(syscall_proc_sig_mask, SYSCALL_PROC_SIG_MASK, void, syscall_proc_sig_mask_trace, int32_t, how, void*, set, void*, retrieve);

static void syscall_proc_sig_mask_trace(syscall_args_t* argument_data, int32_t how, void* set, void* retrieve) {
    (void) argument_data;

    const char* how_name = signal_how_name(how);
    log_print_lockless(LOG_LEVEL_STRC, "how=");
    if(how_name != nullptr) {
        log_print_lockless(LOG_LEVEL_STRC, "%s", how_name);
    } else {
        log_print_lockless(LOG_LEVEL_STRC, "%d", how);
    }
    log_print_lockless(LOG_LEVEL_STRC, ", set=0x%lx, retrieve=0x%lx\n", (unsigned long) (uintptr_t) set, (unsigned long) (uintptr_t) retrieve);
}

static syscall_ret_t syscall_proc_sig_mask(syscall_args_t* argument_data, int32_t how, void* set_ptr, void* retrieve_ptr) {
    (void) argument_data;

    thread_t* thread = sched_arch_thread_current();

    uapi_signal_set_t new_set = 0;

    if(set_ptr != nullptr) {
        if(how != UAPI_SIGNAL_HOW_BLOCK && how != UAPI_SIGNAL_HOW_UNBLOCK && how != UAPI_SIGNAL_HOW_SETMASK) {
            return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);
        }
        vm_copy_from(&new_set, thread->process->address_space, (uintptr_t) set_ptr, sizeof(uapi_signal_set_t));
        new_set = signal_mask_sanitize(new_set);
    }

    spinlock_lock(&thread->signal.lock);
    uapi_signal_set_t old_set = thread->signal.masked_signal_set;

    if(set_ptr != nullptr) {
        switch(how) {
            case UAPI_SIGNAL_HOW_BLOCK:   thread->signal.masked_signal_set |= new_set; break;
            case UAPI_SIGNAL_HOW_UNBLOCK: thread->signal.masked_signal_set &= ~new_set; break;
            case UAPI_SIGNAL_HOW_SETMASK: thread->signal.masked_signal_set = new_set; break;
        }
        LOG_UTRC("old=0x%lx, new=0x%lx\n", new_set, thread->signal.masked_signal_set);
    }

    spinlock_unlock(&thread->signal.lock);

    if(retrieve_ptr != nullptr) {
        vm_copy_to(thread->process->address_space, (uintptr_t) retrieve_ptr, &old_set, sizeof(uapi_signal_set_t));
    }
    return SYSCALL_RET_VALUE(0);
}

SYSCALL_DEFINE_TRACED(syscall_proc_sig_send_simple, SYSCALL_PROC_SIG_SEND_SIMPLE, void, syscall_proc_sig_send_simple_trace, int32_t, pid, uint32_t, tid, int32_t, signal);

static void syscall_proc_sig_send_simple_trace(syscall_args_t* argument_data, int32_t pid, uint32_t tid, int32_t signal) {
    (void) argument_data;

    log_print_lockless(LOG_LEVEL_STRC, "pid=%d, tid=", pid);
    if(tid == UINT32_MAX) {
        log_print_lockless(LOG_LEVEL_STRC, "ALL");
    } else {
        log_print_lockless(LOG_LEVEL_STRC, "%u", tid);
    }
    log_print_lockless(LOG_LEVEL_STRC, ", signal=");
    log_signal_number(signal);
    log_print_lockless(LOG_LEVEL_STRC, "\n");
}

static syscall_ret_t syscall_proc_sig_send_simple(syscall_args_t* argument_data, int32_t pid, uint32_t tid, int32_t signal) {
    (void) argument_data;

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

    spinlock_no_dpc_lock(&target_process->status.lock);
    if(target_process->status.state != PROCESS_RUNNING) {
        spinlock_no_dpc_unlock(&target_process->status.lock);
        process_deref(target_process);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_SEARCH);
    }
    spinlock_no_dpc_unlock(&target_process->status.lock);

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

SYSCALL_DEFINE_NO_RETURN(syscall_proc_sig_send_return, SYSCALL_PROC_SIG_RETURN);
static syscall_ret_t syscall_proc_sig_send_return(syscall_args_t* argument_data) {
    (void) argument_data;
    LOG_UTRC("returning from signal handler\n");

    thread_t* thread = sched_arch_thread_current();

    /// the restorer is reached by returning out of the signal handler, so the user stack pointer points directly at the ucontext that signal_arch_setup_frame pushed.
    userptr_t* ucontext_pointer = userptr_create(argument_data->user_rsp, sizeof(uapi_signal_ucontext_t));
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
