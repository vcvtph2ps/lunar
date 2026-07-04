#pragma once
#include <stdint.h>

typedef struct {
    bool enabled;
} arch_interrupt_state_t;

/**
 * @brief Gets the current interrupt state
 * @return The current interrupt state.
 */
[[nodiscard]] arch_interrupt_state_t arch_interrupt_get_state();

/**
 * @brief Disables interrupts and returns the previous interrupt state.
 * @return The previous interrupt state before disabling.
 */
[[nodiscard]] arch_interrupt_state_t arch_interrupt_disable();

/**
 * @brief Enables interrupts and returns the previous interrupt state.
 * @return The previous interrupt state before enabling.
 */
[[nodiscard]] arch_interrupt_state_t arch_interrupt_enable();

/**
 * @brief Restores the interrupt state to the given state.
 * @param state The interrupt state to restore to.
 */
void arch_interrupt_restore(arch_interrupt_state_t state);
