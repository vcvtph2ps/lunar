#pragma once

#include <common/interrupts/interrupt.h>
#include <common/sched/process.h>
#include <stddef.h>
#include <stdint.h>

typedef enum : uint64_t {
    SYSCALL_HIGHEST_NR
} syscall_nr_t;

typedef enum : int64_t {
    SYSCALL_ERROR_AGAIN = 11, // Try again (EAGAIN)
    SYSCALL_ERROR_NOENT = 2, // No such file or directory
    SYSCALL_ERROR_NOMEM = 12, // Out of memory
    SYSCALL_ERROR_FAULT = 14, // Bad address
    SYSCALL_ERROR_INVAL = 22, // Invalid argument
    SYSCALL_ERROR_NOTTY = 25, // Not a TTY
    SYSCALL_ERROR_SPIPE = 29, // Illegal seek
    SYSCALL_ERROR_ROFS = 30, // Read-only file system
    SYSCALL_ERROR_RANGE = 34, // Out of range
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
    void* arch_frame;
} syscall_args_t;

/**
 * @brief Initializes userspace systems
 */
void syscall_init();

syscall_ret_t syscall_dispatch(syscall_args_t* frame);
