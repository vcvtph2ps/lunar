#pragma once
#include <stdint.h>

typedef struct {
    bool enabled;
} arch_interrupt_state_t;

[[nodiscard]] arch_interrupt_state_t arch_interrupt_disable();
void arch_interrupt_restore(arch_interrupt_state_t state);
