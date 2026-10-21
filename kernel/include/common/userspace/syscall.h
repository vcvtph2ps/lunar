#pragma once

#include <common/interrupts/interrupt.h>
#include <common/sched/process.h>
#include <stddef.h>
#include <stdint.h>

typedef enum : uint64_t {
    SYSCALL_PROC_EXIT = 0,
    SYSCALL_PROC_GETINFO = 1,
    SYSCALL_PROC_SETINFO = 2,
    SYSCALL_PROC_FORK = 3,
    SYSCALL_PROC_WAITPID = 4,
    SYSCALL_PROC_EXECVE = 5,
    SYSCALL_PROC_FEXECVE = 6,

    SYSCALL_PROC_SIG_SEND_SIMPLE = 10,
    SYSCALL_PROC_SIG_QUEUE = 11,

    SYSCALL_PROC_SIG_ACTION = 12,
    SYSCALL_PROC_SIG_MASK = 13,
    SYSCALL_PROC_SIG_ALT_STACK = 14,
    SYSCALL_PROC_SIG_RETURN = 15,

    SYSCALL_PROC_SIG_SUSPEND = 16,
    SYSCALL_PROC_SIG_WAIT = 17,
    SYSCALL_PROC_SIG_PENDING = 18,

    SYSCALL_SYS_DEBUG_LOG = 20,
    SYSCALL_SYS_TCB_SET = 21,

    SYSCALL_VM_MAP = 30,
    SYSCALL_VM_UNMAP = 31,
    SYSCALL_VM_PROTECT = 32,

    SYSCALL_FS_OPEN = 40,
    SYSCALL_FS_CLOSE = 41,
    SYSCALL_FS_READ = 42,
    SYSCALL_FS_WRITE = 43,
    SYSCALL_FS_SEEK = 44,
    SYSCALL_FS_ISATTY = 45,
    SYSCALL_FS_MOUNT = 46,
    SYSCALL_FS_UNMOUNT = 47,

    SYSCALL_HIGHEST_NR
} syscall_nr_t;

typedef enum : int64_t {
    SYSCALL_ERROR_NOENT = 2, // No such file or directory
    SYSCALL_ERROR_SEARCH = 3, // No such process (ESRCH)
    SYSCALL_ERROR_INTR = 4, // Interrupted system call (EINTR)
    SYSCALL_ERROR_TOO_BIG = 7, // Argument list too long
    SYSCALL_ERROR_NO_EXEC = 8, // Exec format error
    SYSCALL_ERROR_CHILD = 10, // No child processes (ECHILD)
    SYSCALL_ERROR_AGAIN = 11, // Try again (EAGAIN)
    SYSCALL_ERROR_NOMEM = 12, // Out of memory
    SYSCALL_ERROR_FAULT = 14, // Bad address
    SYSCALL_ERROR_INVAL = 22, // Invalid argument
    SYSCALL_ERROR_NOTTY = 25, // Not a TTY
    SYSCALL_ERROR_SPIPE = 29, // Illegal seek
    SYSCALL_ERROR_ROFS = 30, // Read-only file system
    SYSCALL_ERROR_RANGE = 34, // Out of range
    SYSCALL_ERROR_LOOP = 40, // Too many levels of symbolic links
    SYSCALL_ERROR_BADFD = 77, // Bad file descriptor
    SYSCALL_ERROR_TIMEDOUT = 110, // Connection timed out (ETIMEDOUT)
} syscall_err_t;

typedef struct [[gnu::packed]] {
    union {
        syscall_err_t err;
        uint64_t value;
    };
    uint64_t is_error;
} syscall_ret_t;

static_assert(sizeof(syscall_ret_t) == 16, "syscall_ret_t must be 16 bytes");

#define SYSCALL_RET_ERROR(err_code) ((syscall_ret_t) { .is_error = true, .err = (err_code) })
#define SYSCALL_RET_VALUE(val) ((syscall_ret_t) { .is_error = false, .value = (val) })

typedef struct {
    uint64_t syscall_nr;
    uint64_t arg1;
    uint64_t arg2;
    uint64_t arg3;
    uint64_t arg4;
    uint64_t arg5;
    uint64_t arg6;
    uint64_t user_rsp;
    arch_interrupt_regs_t* regs;
} syscall_args_t;

/**
 * @brief Initializes userspace systems
 */
void syscall_init();

syscall_ret_t syscall_dispatch(syscall_args_t* frame);
