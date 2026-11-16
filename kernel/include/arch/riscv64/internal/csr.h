#pragma once
#include <stdint.h>

#define ARCH_CSR_READ(csr)                                     \
    ({                                                         \
        uint64_t _val;                                         \
        asm volatile("csrr %0, " #csr : "=r"(_val)::"memory"); \
        _val;                                                  \
    })

#define ARCH_CSR_WRITE(csr, val)                                 \
    do {                                                         \
        uint64_t _v = (uint64_t) (val);                          \
        asm volatile("csrw " #csr ", %0" ::"rK"(_v) : "memory"); \
    } while(0)

#define ARCH_CSR_SET_BITS(csr, bits)                             \
    do {                                                         \
        uint64_t _b = (uint64_t) (bits);                         \
        asm volatile("csrs " #csr ", %0" ::"rK"(_b) : "memory"); \
    } while(0)

#define ARCH_CSR_CLEAR_BITS(csr, bits)                           \
    do {                                                         \
        uint64_t _b = (uint64_t) (bits);                         \
        asm volatile("csrc " #csr ", %0" ::"rK"(_b) : "memory"); \
    } while(0)

#define ARCH_CSR_SWAP(csr, val)                                                    \
    ({                                                                             \
        uint64_t _old;                                                             \
        uint64_t _v = (uint64_t) (val);                                            \
        asm volatile("csrrw %0, " #csr ", %1" : "=r"(_old) : "rK"(_v) : "memory"); \
        _old;                                                                      \
    })

#define ARCH_CSR_SSTATUS_SIE ((uint64_t) (1ULL << 1))
#define ARCH_CSR_SSTATUS_SPIE ((uint64_t) (1ULL << 5))
#define ARCH_CSR_SSTATUS_SPP ((uint64_t) (1ULL << 8))

#define ARCH_CSR_SIE_SSIE ((uint64_t) (1ULL << 1))
#define ARCH_CSR_SIE_STIE ((uint64_t) (1ULL << 5))
#define ARCH_CSR_SIE_SEIE ((uint64_t) (1ULL << 9))

#define ARCH_CSR_SIP_SSIP ((uint64_t) (1ULL << 1))
#define ARCH_CSR_SIP_STIP ((uint64_t) (1ULL << 5))
#define ARCH_CSR_SIP_SEIP ((uint64_t) (1ULL << 9))

#define ARCH_CSR_SSTATUS_FS_MASK ((uint64_t) (3ULL << 13))
#define ARCH_CSR_SSTATUS_FS_INITIAL ((uint64_t) (1ULL << 13))

#define ARCH_CSR_SSTATUS_SUM ((uint64_t) (1ULL << 18))
#define ARCH_CSR_SSTATUS_MXR ((uint64_t) (1ULL << 19))

#define ARCH_CSR_SATP_MODE_SV39 ((uint64_t) (8ULL << 60))
#define ARCH_CSR_SATP_MODE_SV48 ((uint64_t) (9ULL << 60))
#define ARCH_CSR_SATP_MODE_SV57 ((uint64_t) (10ULL << 60))
#define ARCH_CSR_SATP_MODE_MASK ((uint64_t) (0xFULL << 60))

#define ARCH_CSR_SATP_PPN_MASK ((uint64_t) 0x00000fffffffffffull)

#define ARCH_CSR_SATP_MAKE(mode, root_pa) ((mode) | (((root_pa) >> 12) & ARCH_CSR_SATP_PPN_MASK))

#define ARCH_CSR_STVEC_MODE_DIRECT ((uint64_t) 0)
#define ARCH_CSR_STVEC_MODE_VECTORED ((uint64_t) 1)
