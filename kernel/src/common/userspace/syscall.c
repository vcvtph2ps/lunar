#include <common/assert.h>
#include <common/log.h>
#include <common/userspace/syscall.h>

typedef syscall_ret_t (*fn_syscall_handler_t)(syscall_args_t* args);

typedef struct {
    fn_syscall_handler_t handler;
} syscall_entry_t;

static syscall_entry_t g_syscall_table[SYSCALL_HIGHEST_NR];

static const char* userspace_syscall_number_to_string(syscall_nr_t nr) {
    switch(nr) {
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
    LOG_INFO("Invalid syscall \"%s\" (0x%lx) invoked with args: %lu, %lu, %lu, %lu, %lu, %lu\n", userspace_syscall_number_to_string(args->syscall_nr), args->syscall_nr, args->arg1, args->arg2, args->arg3, args->arg4, args->arg5, args->arg6);
    (void) args;

    assert(false);
    return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);
}

#define SYSCALL_DISPATCHER(nr, __handler) g_syscall_table[nr].handler = __handler;

syscall_ret_t syscall_dispatch(syscall_args_t* args) {
    assert(args->syscall_nr < SYSCALL_HIGHEST_NR);
    syscall_entry_t entry = g_syscall_table[args->syscall_nr];
    return entry.handler(args);
}

void arch_syscall_init();

void syscall_init() {
    arch_syscall_init();

    for(size_t i = 0; i < SYSCALL_HIGHEST_NR; i++) {
        g_syscall_table[i].handler = syscall_sys_invalid;
    }

    // SYSCALL_DISPATCHER(SYSCALL_SYS_EXIT, syscall_sys_exit);
    // SYSCALL_DISPATCHER(SYSCALL_DEBUG_LOG, syscall_sys_debug_log);
    // SYSCALL_DISPATCHER(SYSCALL_TCB_SET, syscall_sys_set_tcb);

    // SYSCALL_DISPATCHER(SYSCALL_VM_MAP, syscall_sys_vm_map);
    // SYSCALL_DISPATCHER(SYSCALL_VM_UNMAP, syscall_sys_vm_unmap);
    // SYSCALL_DISPATCHER(SYSCALL_VM_PROTECT, syscall_sys_vm_protect);

    // SYSCALL_DISPATCHER(SYSCALL_OPEN, syscall_sys_open);
    // SYSCALL_DISPATCHER(SYSCALL_CLOSE, syscall_sys_close);
    // SYSCALL_DISPATCHER(SYSCALL_READ, syscall_sys_read);
    // SYSCALL_DISPATCHER(SYSCALL_WRITE, syscall_sys_write);
    // SYSCALL_DISPATCHER(SYSCALL_SEEK, syscall_sys_seek);
    // SYSCALL_DISPATCHER(SYSCALL_ISATTY, syscall_sys_is_a_tty);
    // SYSCALL_DISPATCHER(SYSCALL_GET_CWD, syscall_sys_get_cwd);
    // SYSCALL_DISPATCHER(SYSCALL_STAT, syscall_sys_stat);
    // SYSCALL_DISPATCHER(SYSCALL_STAT_AT, syscall_sys_stat_at);

    // SYSCALL_DISPATCHER(SYSCALL_FUTEX, syscall_sys_futex);
    // SYSCALL_DISPATCHER(SYSCALL_GET_CLOCK, syscall_sys_get_clock);
    // SYSCALL_DISPATCHER(SYSCALL_GET_PROCESS_INFO, syscall_sys_get_process_info);
}
