#pragma once

#include <common/assert.h>
#include <uapi/types.h>

typedef union {
    int sival_int;
    void* sival_ptr;
} uapi_signal_val_t;

typedef struct {
    int signal_number;
    int code;
    int errno;
    uapi_pid_t pid;
    uapi_uid_t uid;
    void* addr;
    int status;
    uapi_signal_val_t value;
} uapi_signal_info_t;

typedef uint64_t uapi_signal_set_t;

typedef struct {
    /// handler address: UAPI_SIG_DEFAULT, UAPI_SIG_IGNORE, or user function pointer
    uintptr_t handler;

    /// restorer function address for sigreturn
    uintptr_t restorer;

    /// Mask to apply during signal execution
    uapi_signal_set_t mask;

    /// SA_* flags
    int flags;
} uapi_signal_handler_t;


#define UAPI_SIGNAL_ERROR (-1)
#define UAPI_SIGNAL_DEFAULT (-2)
#define UAPI_SIGNAL_IGNORE (-3)

/// constants for sigprocmask()
#define UAPI_SIGNAL_HOW_BLOCK 1
#define UAPI_SIGNAL_HOW_UNBLOCK 2
#define UAPI_SIGNAL_HOW_SETMASK 3

typedef enum {
    UAPI_SIGNAL_DEFAULT_ACTION_IGNORE = 0,
    UAPI_SIGNAL_DEFAULT_ACTION_CONTINUE,
    UAPI_SIGNAL_DEFAULT_ACTION_STOP,
    UAPI_SIGNAL_DEFAULT_ACTION_TERMINATE,
    UAPI_SIGNAL_DEFAULT_ACTION_CORE_DUMP
} uapi_signal_default_action_t;

typedef enum {
    UAPI_SIGNAL_HUP = 1,
    UAPI_SIGNAL_INT = 2,
    UAPI_SIGNAL_QUIT = 3,
    UAPI_SIGNAL_CONT = 4,
    UAPI_SIGNAL_BUS = 5,
    UAPI_SIGNAL_ABRT = 6,
    UAPI_SIGNAL_CHLD = 7,
    UAPI_SIGNAL_FPE = 8,
    UAPI_SIGNAL_KILL = 9,
    UAPI_SIGNAL_ILL = 10,
    UAPI_SIGNAL_PIPE = 11,
    UAPI_SIGNAL_SEGV = 12,
    UAPI_SIGNAL_STOP = 13,
    UAPI_SIGNAL_ALRM = 14,
    UAPI_SIGNAL_TERM = 15,
    UAPI_SIGNAL_TSTP = 16,
    UAPI_SIGNAL_TTIN = 17,
    UAPI_SIGNAL_TTOU = 18,
    UAPI_SIGNAL_USR1 = 19,
    UAPI_SIGNAL_USR2 = 20,
    UAPI_SIGNAL_IO = 21,
    UAPI_SIGNAL_PROF = 22,
    UAPI_SIGNAL_SYS = 23,
    UAPI_SIGNAL_TRAP = 24,
    UAPI_SIGNAL_URG = 25,
    UAPI_SIGNAL_VTALRM = 26,
    UAPI_SIGNAL_XCPU = 27,
    UAPI_SIGNAL_XFSZ = 28,
    UAPI_SIGNAL_WINCH = 29,
    UAPI_SIGNAL_PWR = 30,
    UAPI_SIGNAL_CANCEL = 31,
    UAPI_SIGNAL_MAX_SIGNAL = UAPI_SIGNAL_CANCEL,
} uapi_signal_t;

#define UAPI_SIGNAL_POLL UAPI_SIGNAL_IO

static inline bool uapi_sig_uncatchable(int signal_number) {
    return signal_number == UAPI_SIGNAL_KILL || signal_number == UAPI_SIGNAL_STOP;
}

static inline uapi_signal_default_action_t uapi_sig_default_action(int signal_number) {
    switch(signal_number) {
        case UAPI_SIGNAL_CHLD:  [[fallthrough]];
        case UAPI_SIGNAL_URG:   [[fallthrough]];
        case UAPI_SIGNAL_WINCH: return UAPI_SIGNAL_IGNORE;

        case UAPI_SIGNAL_CONT: return UAPI_SIGNAL_DEFAULT_ACTION_CONTINUE;

        case UAPI_SIGNAL_STOP: [[fallthrough]];
        case UAPI_SIGNAL_TSTP: [[fallthrough]];
        case UAPI_SIGNAL_TTIN: [[fallthrough]];
        case UAPI_SIGNAL_TTOU: return UAPI_SIGNAL_DEFAULT_ACTION_STOP;

        case UAPI_SIGNAL_HUP:    [[fallthrough]];
        case UAPI_SIGNAL_INT:    [[fallthrough]];
        case UAPI_SIGNAL_KILL:   [[fallthrough]];
        case UAPI_SIGNAL_PIPE:   [[fallthrough]];
        case UAPI_SIGNAL_ALRM:   [[fallthrough]];
        case UAPI_SIGNAL_TERM:   [[fallthrough]];
        case UAPI_SIGNAL_USR1:   [[fallthrough]];
        case UAPI_SIGNAL_USR2:   [[fallthrough]];
        case UAPI_SIGNAL_IO:     [[fallthrough]];
        case UAPI_SIGNAL_PROF:   [[fallthrough]];
        case UAPI_SIGNAL_VTALRM: [[fallthrough]];
        case UAPI_SIGNAL_PWR:    [[fallthrough]];
        case UAPI_SIGNAL_CANCEL: return UAPI_SIGNAL_DEFAULT_ACTION_TERMINATE;

        case UAPI_SIGNAL_QUIT: [[fallthrough]];
        case UAPI_SIGNAL_BUS:  [[fallthrough]];
        case UAPI_SIGNAL_ABRT: [[fallthrough]];
        case UAPI_SIGNAL_FPE:  [[fallthrough]];
        case UAPI_SIGNAL_ILL:  [[fallthrough]];
        case UAPI_SIGNAL_SEGV: [[fallthrough]];
        case UAPI_SIGNAL_SYS:  [[fallthrough]];
        case UAPI_SIGNAL_TRAP: [[fallthrough]];
        case UAPI_SIGNAL_XCPU: [[fallthrough]];
        case UAPI_SIGNAL_XFSZ: return UAPI_SIGNAL_DEFAULT_ACTION_CORE_DUMP;
    };

    ASSERT_UNREACHABLE();
}
