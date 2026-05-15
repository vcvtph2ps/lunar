#pragma once
#include <stdint.h>

static inline void arch_io_port_write_u8(uint16_t port, uint8_t value) {
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void arch_io_mem_write_u8(uintptr_t addr, uint8_t value) {
    asm volatile("mfence\nmovb %1, (%0)" : : "r"(addr), "q"(value) : "memory");
}

static inline void arch_io_mem_write_u16(uintptr_t addr, uint16_t value) {
    asm volatile("mfence\nmovw %1, (%0)" : : "r"(addr), "q"(value) : "memory");
}

static inline void arch_io_mem_write_u32(uintptr_t addr, uint32_t value) {
    asm volatile("mfence\nmovl %1, (%0)" : : "r"(addr), "q"(value) : "memory");
}

static inline void arch_io_mem_write_u64(uintptr_t addr, uint64_t value) {
    asm volatile("mfence\nmovq %1, (%0)" : : "r"(addr), "q"(value) : "memory");
}

static inline uint8_t arch_io_mem_read_u8(uintptr_t addr) {
    uint8_t ret;
    asm volatile("mfence\nmovb (%1), %0" : "=q"(ret) : "r"(addr) : "memory");
    return ret;
}
static inline uint16_t arch_io_mem_read_u16(uintptr_t addr) {
    uint16_t ret;
    asm volatile("mfence\nmovw (%1), %0" : "=q"(ret) : "r"(addr) : "memory");
    return ret;
}
static inline uint32_t arch_io_mem_read_u32(uintptr_t addr) {
    uint32_t ret;
    asm volatile("mfence\nmovl (%1), %0" : "=q"(ret) : "r"(addr) : "memory");
    return ret;
}
static inline uint64_t arch_io_mem_read_u64(uintptr_t addr) {
    uint64_t ret;
    asm volatile("mfence\nmovq (%1), %0" : "=q"(ret) : "r"(addr) : "memory");
    return ret;
}
