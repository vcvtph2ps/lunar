#include <common/assert.h>
#include <common/cpu_local.h>
#include <common/log.h>
#include <common/userspace/syscall.h>
#include <common/userspace/syscall_defs.h>
#include <common/userspace/userspace.h>

typedef syscall_ret_t (*fn_syscall_handler_t)(syscall_args_t* args);

static fn_syscall_handler_t g_syscall_table[SYSCALL_HIGHEST_NR];

static const char* userspace_syscall_number_to_string(syscall_nr_t nr) { // NOLINT
    switch(nr) {
        case SYSCALL_PROC_EXIT:    return "SYSCALL_PROC_EXIT";
        case SYSCALL_PROC_GETINFO: return "SYSCALL_PROC_GETINFO";
        case SYSCALL_PROC_SETINFO: return "SYSCALL_PROC_SETINFO";
        case SYSCALL_PROC_FORK:    return "SYSCALL_PROC_FORK";
        case SYSCALL_PROC_WAITPID: return "SYSCALL_PROC_WAITPID";
        case SYSCALL_PROC_EXECVE:  return "SYSCALL_PROC_EXECVE";
        case SYSCALL_PROC_FEXECVE: return "SYSCALL_PROC_FEXECVE";

        case SYSCALL_PROC_SIG_SEND_SIMPLE: return "SYSCALL_PROC_SIG_SEND_SIMPLE";
        case SYSCALL_PROC_SIG_QUEUE:       return "SYSCALL_PROC_SIG_QUEUE";
        case SYSCALL_PROC_SIG_ACTION:      return "SYSCALL_PROC_SIG_ACTION";
        case SYSCALL_PROC_SIG_MASK:        return "SYSCALL_PROC_SIG_MASK";
        case SYSCALL_PROC_SIG_ALT_STACK:   return "SYSCALL_PROC_SIG_ALT_STACK";
        case SYSCALL_PROC_SIG_RETURN:      return "SYSCALL_PROC_SIG_RETURN";
        case SYSCALL_PROC_SIG_SUSPEND:     return "SYSCALL_PROC_SIG_SUSPEND";
        case SYSCALL_PROC_SIG_WAIT:        return "SYSCALL_PROC_SIG_WAIT";
        case SYSCALL_PROC_SIG_PENDING:     return "SYSCALL_PROC_SIG_PENDING";

        case SYSCALL_SYS_TCB_SET:   return "SYSCALL_SYS_TCB_SET";
        case SYSCALL_SYS_DEBUG_LOG: return "SYSCALL_SYS_DEBUG_LOG";

        case SYSCALL_VM_MAP:     return "SYSCALL_VM_MAP";
        case SYSCALL_VM_UNMAP:   return "SYSCALL_VM_UNMAP";
        case SYSCALL_VM_PROTECT: return "SYSCALL_VM_PROTECT";

        case SYSCALL_FS_OPEN:    return "SYSCALL_FS_OPEN";
        case SYSCALL_FS_CLOSE:   return "SYSCALL_FS_CLOSE";
        case SYSCALL_FS_READ:    return "SYSCALL_FS_READ";
        case SYSCALL_FS_WRITE:   return "SYSCALL_FS_WRITE";
        case SYSCALL_FS_SEEK:    return "SYSCALL_FS_SEEK";
        case SYSCALL_FS_ISATTY:  return "SYSCALL_FS_ISATTY";
        case SYSCALL_FS_MOUNT:   return "SYSCALL_FS_MOUNT";
        case SYSCALL_FS_UNMOUNT: return "SYSCALL_FS_UNMOUNT";
        case SYSCALL_FS_GETCWD:  return "SYSCALL_FS_GETCWD";
        case SYSCALL_FS_CHDIR:   return "SYSCALL_FS_CHDIR";
        case SYSCALL_FS_GETDENTS: return "SYSCALL_FS_GETDENTS";
        case SYSCALL_FS_READLINKAT: return "SYSCALL_FS_READLINKAT";

        default: return "Unknown syscall";
    }
}

static const char* userspace_syscall_ret_to_string(syscall_ret_t ret) { // NOLINT
    if(!ret.is_error) {
        return "SUCCESS";
    }
    switch(ret.err) {
        case SYSCALL_ERROR_NOENT:  return "ERROR_NOENT";
        case SYSCALL_ERROR_SEARCH: return "ERROR_SEARCH";
        case SYSCALL_ERROR_CHILD:  return "ERROR_CHILD";
        case SYSCALL_ERROR_NOMEM:  return "ERROR_NOMEM";
        case SYSCALL_ERROR_FAULT:  return "ERROR_FAULT";
        case SYSCALL_ERROR_INVAL:  return "ERROR_INVAL";
        case SYSCALL_ERROR_NOTDIR: return "ERROR_NOTDIR";
        case SYSCALL_ERROR_SPIPE:  return "ERROR_SPIPE";
        case SYSCALL_ERROR_ROFS:   return "ERROR_ROFS";
        case SYSCALL_ERROR_RANGE:  return "ERROR_RANGE";
        case SYSCALL_ERROR_BADFD:  return "ERROR_BADFD";
        case SYSCALL_ERROR_AGAIN:  return "ERROR_AGAIN";
        default:                   arch_panic("Unknown syscall error code: %lu", ret.err);
    }
}

static syscall_ret_t syscall_sys_invalid(syscall_args_t* args) {
    (void) args;

    LOG_INFO("Invalid syscall \"%s\" (0x%lx) invoked with args: %lu, %lu, %lu, %lu, %lu, %lu\n", userspace_syscall_number_to_string(args->syscall_nr), args->syscall_nr, args->arg1, args->arg2, args->arg3, args->arg4, args->arg5, args->arg6);
    user_assert(false && "Invalid syscall");
    return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);
}

#define SYSCALL_DISPATCHER(nr, __handler) g_syscall_table[nr] = __handler;

syscall_ret_t syscall_dispatch(syscall_args_t* args) {
    user_assert(args->syscall_nr < SYSCALL_HIGHEST_NR);
    fn_syscall_handler_t handler = g_syscall_table[args->syscall_nr];
    user_assert(handler != nullptr);

    return handler(args);
}

void arch_syscall_init();

void syscall_init() {
    arch_syscall_init();

    for(size_t i = 0; i < SYSCALL_HIGHEST_NR; i++) {
        g_syscall_table[i] = syscall_sys_invalid;
    }

    SYSCALL_DISPATCHER(SYSCALL_PROC_EXIT, syscall_sys_proc_exit);
    SYSCALL_DISPATCHER(SYSCALL_PROC_GETINFO, syscall_sys_proc_getinfo);
    SYSCALL_DISPATCHER(SYSCALL_PROC_SETINFO, syscall_sys_proc_setinfo);
    SYSCALL_DISPATCHER(SYSCALL_PROC_FORK, syscall_sys_proc_fork);
    SYSCALL_DISPATCHER(SYSCALL_PROC_WAITPID, syscall_sys_proc_waitpid);
    SYSCALL_DISPATCHER(SYSCALL_PROC_EXECVE, syscall_sys_proc_execve);
    SYSCALL_DISPATCHER(SYSCALL_PROC_FEXECVE, syscall_sys_proc_fexecve);

    SYSCALL_DISPATCHER(SYSCALL_PROC_SIG_ACTION, syscall_sys_proc_sig_action);
    SYSCALL_DISPATCHER(SYSCALL_PROC_SIG_MASK, syscall_sys_proc_sig_mask);
    SYSCALL_DISPATCHER(SYSCALL_PROC_SIG_SEND_SIMPLE, syscall_sys_proc_sig_send_simple);
    SYSCALL_DISPATCHER(SYSCALL_PROC_SIG_RETURN, syscall_sys_proc_sig_send_return);

    SYSCALL_DISPATCHER(SYSCALL_SYS_TCB_SET, syscall_sys_thread_set_tcb);
    SYSCALL_DISPATCHER(SYSCALL_SYS_DEBUG_LOG, syscall_sys_debug_log);

    SYSCALL_DISPATCHER(SYSCALL_VM_MAP, syscall_sys_vm_map);
    SYSCALL_DISPATCHER(SYSCALL_VM_UNMAP, syscall_sys_vm_unmap);
    SYSCALL_DISPATCHER(SYSCALL_VM_PROTECT, syscall_sys_vm_protect);

    SYSCALL_DISPATCHER(SYSCALL_FS_OPEN, syscall_sys_fs_open);
    SYSCALL_DISPATCHER(SYSCALL_FS_CLOSE, syscall_sys_fs_close);
    SYSCALL_DISPATCHER(SYSCALL_FS_READ, syscall_sys_fs_read);
    SYSCALL_DISPATCHER(SYSCALL_FS_WRITE, syscall_sys_fs_write);
    SYSCALL_DISPATCHER(SYSCALL_FS_SEEK, syscall_sys_fs_seek);
    SYSCALL_DISPATCHER(SYSCALL_FS_ISATTY, syscall_sys_fs_is_a_tty);
    SYSCALL_DISPATCHER(SYSCALL_FS_GETCWD, syscall_sys_fs_getcwd);
    SYSCALL_DISPATCHER(SYSCALL_FS_CHDIR, syscall_sys_fs_chdir);
    SYSCALL_DISPATCHER(SYSCALL_FS_STAT, syscall_sys_fs_stat);
    SYSCALL_DISPATCHER(SYSCALL_FS_STAT_AT, syscall_sys_fs_stat_at);
    SYSCALL_DISPATCHER(SYSCALL_FS_DUP, syscall_sys_fs_dup);
    SYSCALL_DISPATCHER(SYSCALL_FS_DUP2, syscall_sys_fs_dup2);
    SYSCALL_DISPATCHER(SYSCALL_FS_GETDENTS, syscall_sys_fs_getdents);
    SYSCALL_DISPATCHER(SYSCALL_FS_READLINKAT, syscall_sys_fs_readlinkat);

    // SYSCALL_DISPATCHER(SYSCALL_FUTEX, syscall_sys_futex);
    // SYSCALL_DISPATCHER(SYSCALL_GET_CLOCK, syscall_sys_get_clock);
    // SYSCALL_DISPATCHER(SYSCALL_GET_PROCESS_INFO, syscall_sys_get_process_info);
}
