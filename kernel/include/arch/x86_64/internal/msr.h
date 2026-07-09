#pragma once
#include <stdint.h>

#define ARCH_MSR_EFER 0xC0000080
#define ARCH_MSR_ACTIVE_GS_BASE 0xC0000101

#define ARCH_MSR_APIC_BASE_MSR 0x1B
#define ARCH_MSR_X2APIC_BASE_MSR 0x800

#define ARCH_MSR_KVM_PVCLOCK 0x4B564D01

[[nodiscard]] static inline uint64_t arch_msr_read(uint64_t msr) {
    uint32_t low;
    uint32_t high;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"((uint32_t) msr));
    return low + ((uint64_t) high << 32);
}

static inline void arch_msr_write(uint64_t msr, uint64_t value) {
    asm volatile("wrmsr" : : "a"((uint32_t) value), "d"((uint32_t) (value >> 32)), "c"((uint32_t) msr));
}
