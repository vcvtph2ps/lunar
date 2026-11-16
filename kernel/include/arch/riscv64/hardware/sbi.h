#pragma once

#include <stdint.h>

// Completed successfully
#define ARCH_SBI_SUCCESS 0
// Failed
#define ARCH_SBI_ERR_FAILED -1
// Not supported
#define ARCH_SBI_ERR_NOT_SUPPORTED -2
// Invalid parameter(s)
#define ARCH_SBI_ERR_INVALID_PARAM -3
// Denied or not allowed
#define ARCH_SBI_ERR_DENIED -4
// Invalid address(s)
#define ARCH_SBI_ERR_INVALID_ADDRESS -5
// Already available
#define ARCH_SBI_ERR_ALREADY_AVAILABLE -6
// Already started
#define ARCH_SBI_ERR_ALREADY_STARTED -7
// Already stopped
#define ARCH_SBI_ERR_ALREADY_STOPPED -8
// Shared memory not available
#define ARCH_SBI_ERR_NO_SHMEM -9
// Invalid state
#define ARCH_SBI_ERR_INVALID_STATE -10
// Bad (or invalid) range
#define ARCH_SBI_ERR_BAD_RANGE -11
// Failed due to timeout
#define ARCH_SBI_ERR_TIMEOUT -12
// Input/Output error
#define ARCH_SBI_ERR_IO -13
// Denied or not allowed due to lock status
#define ARCH_SBI_ERR_DENIED_LOCKED -14


typedef struct {
    long error;
    long value;
} arch_sbi_ret_t;

static inline const char* arch_sbi_return_to_string(arch_sbi_ret_t ret) {
    if(!ret.error) {
        return "SBI_SUCCESS";
    }

    switch(ret.value) {
        case ARCH_SBI_ERR_FAILED:            return "SBI_FAILED";
        case ARCH_SBI_ERR_NOT_SUPPORTED:     return "SBI_NOT_SUPPORTED";
        case ARCH_SBI_ERR_INVALID_PARAM:     return "SBI_INVALID_PARAM";
        case ARCH_SBI_ERR_DENIED:            return "SBI_DENIED";
        case ARCH_SBI_ERR_INVALID_ADDRESS:   return "SBI_INVALID_ADDRESS";
        case ARCH_SBI_ERR_ALREADY_AVAILABLE: return "SBI_ALREADY_AVAILABLE";
        case ARCH_SBI_ERR_ALREADY_STARTED:   return "SBI_ALREADY_STARTED";
        case ARCH_SBI_ERR_ALREADY_STOPPED:   return "SBI_ALREADY_STOPPED";
        case ARCH_SBI_ERR_NO_SHMEM:          return "SBI_NO_SHMEM";
        case ARCH_SBI_ERR_INVALID_STATE:     return "SBI_INVALID_STATE";
        case ARCH_SBI_ERR_BAD_RANGE:         return "SBI_BAD_RANGE";
        case ARCH_SBI_ERR_TIMEOUT:           return "SBI_TIMEOUT";
        case ARCH_SBI_ERR_IO:                return "SBI_IO";
        case ARCH_SBI_ERR_DENIED_LOCKED:     return "SBI_DENIED_LOCKED";
        default:                             return "Unknown sbi error";
    }
}

static inline arch_sbi_ret_t arch_sbi_ecall(long ext, long fid, long arg0, long arg1, long arg2, long arg3, long arg4, long arg5) {
    register long a0 asm("a0") = arg0;
    register long a1 asm("a1") = arg1;
    register long a2 asm("a2") = arg2;
    register long a3 asm("a3") = arg3;
    register long a4 asm("a4") = arg4;
    register long a5 asm("a5") = arg5;
    register long a6 asm("a6") = fid;
    register long a7 asm("a7") = ext;
    asm volatile("ecall" : "+r"(a0), "+r"(a1) : "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6), "r"(a7) : "memory");
    return (arch_sbi_ret_t) { .error = a0, .value = a1 };
}

static inline arch_sbi_ret_t arch_sbi_ecall0(long ext, long fid) {
    return arch_sbi_ecall(ext, fid, 0, 0, 0, 0, 0, 0);
}
static inline arch_sbi_ret_t arch_sbi_ecall1(long ext, long fid, long a0) {
    return arch_sbi_ecall(ext, fid, a0, 0, 0, 0, 0, 0);
}
static inline arch_sbi_ret_t arch_sbi_ecall2(long ext, long fid, long a0, long a1) {
    return arch_sbi_ecall(ext, fid, a0, a1, 0, 0, 0, 0);
}
static inline arch_sbi_ret_t arch_sbi_ecall3(long ext, long fid, long a0, long a1, long a2) {
    return arch_sbi_ecall(ext, fid, a0, a1, a2, 0, 0, 0);
}
static inline arch_sbi_ret_t arch_sbi_ecall4(long ext, long fid, long a0, long a1, long a2, long a3) {
    return arch_sbi_ecall(ext, fid, a0, a1, a2, a3, 0, 0);
}
static inline arch_sbi_ret_t arch_sbi_ecall5(long ext, long fid, long a0, long a1, long a2, long a3, long a4) {
    return arch_sbi_ecall(ext, fid, a0, a1, a2, a3, a4, 0);
}
static inline arch_sbi_ret_t arch_sbi_ecall6(long ext, long fid, long a0, long a1, long a2, long a3, long a4, long a5) {
    return arch_sbi_ecall(ext, fid, a0, a1, a2, a3, a4, a5);
}

#define ARCH_SBI_EXT_LEGACY_PUTCHAR 0x01
static inline long arch_sbi_console_putchar(int c) {
    return arch_sbi_ecall1(ARCH_SBI_EXT_LEGACY_PUTCHAR, 0, c).error;
}

#define ARCH_SBI_EXT_IPI 0x735049
#define ARCH_SBI_IPI_SEND 0

/**
 * @brief Raises a supervisor software interrupt on a set of harts
 * @param hart_mask bitmask of harts to target relative to hart_mask_base
 * @param hart_mask_base first hart id the mask applies to
 */
static inline arch_sbi_ret_t arch_sbi_send_ipi(long hart_mask, long hart_mask_base) {
    return arch_sbi_ecall2(ARCH_SBI_EXT_IPI, ARCH_SBI_IPI_SEND, hart_mask, hart_mask_base);
}

#define ARCH_SBI_EXT_TIME 0x54494D45
#define ARCH_SBI_TIME_SET_TIMER 0

/**
 * @brief programs the supervisor timer to fire at the given absolute time
 * @param deadline absolute time value, uses the time csr
 */
static inline arch_sbi_ret_t arch_sbi_set_timer(uint64_t deadline) {
    return arch_sbi_ecall1(ARCH_SBI_EXT_TIME, ARCH_SBI_TIME_SET_TIMER, (long) deadline);
}

#define ARCH_SBI_EXT_HSM 0x48534D
#define ARCH_SBI_HSM_START_HART 0

/**
 * @brief Starts a given hart
 * @param hart_id the target hart to start
 * @param start_address physical address to start execution at
 * @param param loaded into a1 on start
 */
static inline arch_sbi_ret_t sbi_start_hart(uint64_t hart_id, uint64_t start_address, uint64_t param) {
    return arch_sbi_ecall3(ARCH_SBI_EXT_HSM, ARCH_SBI_HSM_START_HART, hart_id, start_address, param);
}
