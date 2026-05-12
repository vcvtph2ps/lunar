#pragma once
#include <stdint.h>

static inline void arch_io_port_write_u8(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}
