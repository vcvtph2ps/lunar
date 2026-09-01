#include <arch/x86_64/cpu_local.h>
#include <common/assert.h>
#include <common/log.h>
#include <common/userspace/syscall.h>
#include <common/userspace/syscall_defs.h>
#include <common/userspace/userspace.h>

typedef syscall_ret_t (*fn_syscall_handler_t)(syscall_args_t* args);

static fn_syscall_handler_t g_syscall_table[SYSCALL_HIGHEST_NR];

static const char* userspace_syscall_number_to_string(syscall_nr_t nr) { // NOLINT
    switch(nr) {
        case SYSCALL_PROC_EXIT:      return "SYSCALL_EXIT";
        case SYSCALL_THREAD_TCB_SET: return "SYSCALL_TCB_SET";
        case SYSCALL_DEBUG_LOG:      return "SYSCALL_DEBUG_LOG";

        case SYSCALL_VM_MAP:     return "SYSCALL_VM_MAP";
        case SYSCALL_VM_UNMAP:   return "SYSCALL_VM_UNMAP";
        case SYSCALL_VM_PROTECT: return "SYSCALL_VM_PROTECT";

        case SYSCALL_FS_OPEN:   return "SYSCALL_FS_OPEN";
        case SYSCALL_FS_CLOSE:  return "SYSCALL_FS_CLOSE";
        case SYSCALL_FS_READ:   return "SYSCALL_FS_READ";
        case SYSCALL_FS_WRITE:  return "SYSCALL_FS_WRITE";
        case SYSCALL_FS_SEEK:   return "SYSCALL_FS_SEEK";
        case SYSCALL_FS_ISATTY: return "SYSCALL_FS_ISATTY";

        default: return "Unknown syscall";
    }
}

static const char* userspace_syscall_ret_to_string(syscall_ret_t ret) { // NOLINT
    if(!ret.is_error) {
        return "SUCCESS";
    }
    switch(ret.err) {
        case SYSCALL_ERROR_NOENT: return "ERROR_NOENT";
        case SYSCALL_ERROR_NOMEM: return "ERROR_NOMEM";
        case SYSCALL_ERROR_FAULT: return "ERROR_FAULT";
        case SYSCALL_ERROR_INVAL: return "ERROR_INVAL";
        case SYSCALL_ERROR_SPIPE: return "ERROR_SPIPE";
        case SYSCALL_ERROR_ROFS:  return "ERROR_ROFS";
        case SYSCALL_ERROR_RANGE: return "ERROR_RANGE";
        case SYSCALL_ERROR_BADFD: return "ERROR_BADFD";
        case SYSCALL_ERROR_AGAIN: return "ERROR_AGAIN";
        default:                  arch_panic("Unknown syscall error code: %lu", ret.err);
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
    SYSCALL_DISPATCHER(SYSCALL_THREAD_TCB_SET, syscall_sys_thread_set_tcb);
    SYSCALL_DISPATCHER(SYSCALL_DEBUG_LOG, syscall_sys_debug_log);

    SYSCALL_DISPATCHER(SYSCALL_VM_MAP, syscall_sys_vm_map);
    SYSCALL_DISPATCHER(SYSCALL_VM_UNMAP, syscall_sys_vm_unmap);
    SYSCALL_DISPATCHER(SYSCALL_VM_PROTECT, syscall_sys_vm_protect);

    SYSCALL_DISPATCHER(SYSCALL_FS_OPEN, syscall_sys_fs_open);
    SYSCALL_DISPATCHER(SYSCALL_FS_CLOSE, syscall_sys_fs_close);
    SYSCALL_DISPATCHER(SYSCALL_FS_READ, syscall_sys_fs_read);
    // SYSCALL_DISPATCHER(SYSCALL_FS_WRITE, syscall_sys_fs_write);
    SYSCALL_DISPATCHER(SYSCALL_FS_SEEK, syscall_sys_fs_seek);
    // SYSCALL_DISPATCHER(SYSCALL_FS_ISATTY, syscall_sys_fs_is_a_tty);
    // SYSCALL_DISPATCHER(SYSCALL_FS_GET_CWD, syscall_sys_fs_get_cwd);
    // SYSCALL_DISPATCHER(SYSCALL_FS_STAT, syscall_sys_fs_stat);
    // SYSCALL_DISPATCHER(SYSCALL_FS_STAT_AT, syscall_sys_fs_stat_at);

    // SYSCALL_DISPATCHER(SYSCALL_FUTEX, syscall_sys_futex);
    // SYSCALL_DISPATCHER(SYSCALL_GET_CLOCK, syscall_sys_get_clock);
    // SYSCALL_DISPATCHER(SYSCALL_GET_PROCESS_INFO, syscall_sys_get_process_info);
}
