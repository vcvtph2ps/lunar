#pragma once

#include <stdint.h>

#define DEFINE_WRITE(REGISTER)                                           \
    static inline void arch_cr_write_##REGISTER(uint64_t value) {        \
        asm volatile("movq %0, %%" #REGISTER : : "r"(value) : "memory"); \
    }

#define DEFINE_READ(REGISTER)                                        \
    [[nodiscard]] static inline uint64_t arch_cr_read_##REGISTER() { \
        uint64_t value;                                              \
        asm volatile("movq %%" #REGISTER ", %0" : "=r"(value));      \
        return value;                                                \
    }

DEFINE_READ(cr0)
DEFINE_WRITE(cr0)

DEFINE_READ(cr2)

DEFINE_READ(cr3)
DEFINE_WRITE(cr3)

DEFINE_READ(cr4)
DEFINE_WRITE(cr4)

DEFINE_READ(cr8)
DEFINE_WRITE(cr8)

static inline void arch_cr_write_xcr0(uint64_t value) {
    asm volatile("xsetbv" : : "a"(value), "d"(value >> 32), "c"(0) : "memory");
}

static inline uint64_t arch_cr_read_xcr0() {
    uint32_t hi, lo;
    asm volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
    return ((uint64_t) hi << 32) | lo;
}

#undef DEFINE_READ
#undef DEFINE_WRITE
