#include <common/assert.h>
#include <common/cpu_local.h>
#include <common/log.h>
#include <common/userspace/syscall.h>
#include <common/userspace/syscall_defs.h>
#include <common/userspace/userspace.h>

static const syscall_meta_t* g_syscall_table[SYSCALL_HIGHEST_NR];

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

        case SYSCALL_FS_OPEN:       return "SYSCALL_FS_OPEN";
        case SYSCALL_FS_CLOSE:      return "SYSCALL_FS_CLOSE";
        case SYSCALL_FS_READ:       return "SYSCALL_FS_READ";
        case SYSCALL_FS_WRITE:      return "SYSCALL_FS_WRITE";
        case SYSCALL_FS_SEEK:       return "SYSCALL_FS_SEEK";
        case SYSCALL_FS_ISATTY:     return "SYSCALL_FS_ISATTY";
        case SYSCALL_FS_MOUNT:      return "SYSCALL_FS_MOUNT";
        case SYSCALL_FS_UNMOUNT:    return "SYSCALL_FS_UNMOUNT";
        case SYSCALL_FS_GETCWD:     return "SYSCALL_FS_GETCWD";
        case SYSCALL_FS_CHDIR:      return "SYSCALL_FS_CHDIR";
        case SYSCALL_FS_GETDENTS:   return "SYSCALL_FS_GETDENTS";
        case SYSCALL_FS_READLINKAT: return "SYSCALL_FS_READLINKAT";

        default: return "Unknown syscall";
    }
}

static const char* userspace_syscall_ret_to_string(syscall_ret_t ret) { // NOLINT
    if(!ret.is_error) {
        return "SUCCESS";
    }
    switch(ret.err) {
        case SYSCALL_ERROR_NOENT:    return "ERROR_NOENT";
        case SYSCALL_ERROR_SEARCH:   return "ERROR_SEARCH";
        case SYSCALL_ERROR_INTR:     return "ERROR_INTR";
        case SYSCALL_ERROR_TOO_BIG:  return "ERROR_TOO_BIG";
        case SYSCALL_ERROR_NO_EXEC:  return "ERROR_NO_EXEC";
        case SYSCALL_ERROR_CHILD:    return "ERROR_CHILD";
        case SYSCALL_ERROR_AGAIN:    return "ERROR_AGAIN";
        case SYSCALL_ERROR_NOMEM:    return "ERROR_NOMEM";
        case SYSCALL_ERROR_FAULT:    return "ERROR_FAULT";
        case SYSCALL_ERROR_NOTDIR:   return "ERROR_NOTDIR";
        case SYSCALL_ERROR_ISDIR:    return "ERROR_ISDIR";
        case SYSCALL_ERROR_INVAL:    return "ERROR_INVAL";
        case SYSCALL_ERROR_NOSYS:    return "ERROR_NOSYS";
        case SYSCALL_ERROR_NOTTY:    return "ERROR_NOTTY";
        case SYSCALL_ERROR_SPIPE:    return "ERROR_SPIPE";
        case SYSCALL_ERROR_ROFS:     return "ERROR_ROFS";
        case SYSCALL_ERROR_RANGE:    return "ERROR_RANGE";
        case SYSCALL_ERROR_LOOP:     return "ERROR_LOOP";
        case SYSCALL_ERROR_BADFD:    return "ERROR_BADFD";
        case SYSCALL_ERROR_TIMEDOUT: return "ERROR_TIMEDOUT";
        default:                     arch_panic("Unknown syscall error code: %lu", ret.err);
    }
}

syscall_ret_t syscall_from_vfs_result(vfs_result_t result) {
    switch(result) {
        case VFS_RESULT_OK:              return SYSCALL_RET_VALUE(0);
        case VFS_RESULT_ERR_NOT_FOUND:   return SYSCALL_RET_ERROR(SYSCALL_ERROR_NOENT);
        case VFS_RESULT_ERR_NOT_DIR:     return SYSCALL_RET_ERROR(SYSCALL_ERROR_NOTDIR);
        case VFS_RESULT_ERR_IS_DIR:      return SYSCALL_RET_ERROR(SYSCALL_ERROR_ISDIR);
        case VFS_RESULT_ERR_LOOP:        return SYSCALL_RET_ERROR(SYSCALL_ERROR_LOOP);
        case VFS_RESULT_ERR_WOULD_BLOCK: return SYSCALL_RET_ERROR(SYSCALL_ERROR_AGAIN);
        case VFS_RESULT_ERR_INTERRUPTED: return SYSCALL_RET_ERROR(SYSCALL_ERROR_INTR);
        default:                         user_assert(false && "Invalid vfs return value");
    }
}

static void print_value_with_type(sc_kind_t kind, uint64_t value) {
    switch(kind) {
        case SC_U64:  log_print_lockless(LOG_LEVEL_STRC, "%lu", (int64_t) value); break;
        case SC_I64:  log_print_lockless(LOG_LEVEL_STRC, "%ld", (uint64_t) value); break;
        case SC_U32:  log_print_lockless(LOG_LEVEL_STRC, "%u", (int32_t) value); break;
        case SC_I32:  log_print_lockless(LOG_LEVEL_STRC, "%i", (uint32_t) value); break;
        case SC_PTR:  log_print_lockless(LOG_LEVEL_STRC, "0x%lx", (int64_t) value); break;
        case SC_VOID: log_print_lockless(LOG_LEVEL_STRC, "<void>"); break;
    }
}

static void syscall_log(const syscall_meta_t* meta, const syscall_args_t* arguments, syscall_ret_t ret) {
    arch_interrupt_state_t state = log_lock_acquire();
    log_print_lockless(LOG_LEVEL_STRC, LOG_COLORIZE("utrc | ", "208") "(pid: %d) %s: (", sched_arch_thread_current()->process->process_id, meta->name);
    for(size_t i = 0; i < meta->nargs; i++) {
        const syscall_arg_desc_t* d = &meta->args[i];
        log_print_lockless(LOG_LEVEL_STRC, "%s%s=", i ? ", " : "", d->name);
        print_value_with_type(d->kind, arguments->args[i]);
    }
    log_print_lockless(LOG_LEVEL_STRC, ") = ");
    if(!ret.is_error) {
        print_value_with_type(meta->ret_type, ret.value);
        log_print_lockless(LOG_LEVEL_STRC, "\n");
    } else {
        log_print_lockless(LOG_LEVEL_STRC, "%s\n", userspace_syscall_ret_to_string(ret));
    }

    if(meta->ret_type == SC_VOID && ret.value != 0 && !ret.is_error) {
        arch_panic("void syscall returned with value 0x%lx\n", ret.value);
    }


    log_lock_release(state);
}

static void syscall_log_noreturn(const syscall_meta_t* meta, const syscall_args_t* arguments) {
    arch_interrupt_state_t state = log_lock_acquire();
    log_print_lockless(LOG_LEVEL_STRC, LOG_COLORIZE("utrc | ", "208") "(pid: %d) %s: (", sched_arch_thread_current()->process->process_id, meta->name);
    for(size_t i = 0; i < meta->nargs; i++) {
        const syscall_arg_desc_t* d = &meta->args[i];
        log_print_lockless(LOG_LEVEL_STRC, "%s%s=", i ? ", " : "", d->name);
        print_value_with_type(d->kind, arguments->args[i]);
    }
    log_print_lockless(LOG_LEVEL_STRC, ") = [no return]\n");
    log_lock_release(state);
}

syscall_ret_t syscall_dispatch(syscall_args_t* args) {
    user_assert(args->syscall_nr < SYSCALL_HIGHEST_NR);

    const syscall_meta_t* meta = g_syscall_table[args->syscall_nr];
    if(meta == nullptr) {
        LOG_INFO(
            "Invalid syscall \"%s\" (0x%lx) invoked with args: %lu, %lu, %lu, %lu, %lu, %lu\n",
            userspace_syscall_number_to_string(args->syscall_nr),
            args->syscall_nr,
            args->args[0],
            args->args[1],
            args->args[2],
            args->args[3],
            args->args[4],
            args->args[5]
        );
        user_assert(false && "Invalid syscall");
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);
    }
    user_assert(meta != nullptr);

#if defined(__DEBUG__)
    if(meta->no_return) {
        syscall_log_noreturn(meta, args);
    }
#endif
    syscall_ret_t ret = meta->fn(args);
#if defined(__DEBUG__)
    syscall_log(meta, args, ret);
#endif
    return ret;
}

void arch_syscall_init();

extern const syscall_meta_t __start_syscall_meta[], __stop_syscall_meta[]; // NOLINT

void syscall_init() {
    arch_syscall_init();

    for(size_t i = 0; i < SYSCALL_HIGHEST_NR; i++) {
        g_syscall_table[i] = nullptr;
    }

    for(const syscall_meta_t* m = __start_syscall_meta; m < __stop_syscall_meta; m++) {
        assert(g_syscall_table[m->num] == nullptr);
        g_syscall_table[m->num] = m;
    }
}
