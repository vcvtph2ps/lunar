#pragma once
#include <stdint.h>

typedef struct {
    // @todo:
} arch_interrupt_regs_t;

typedef struct {
    // @todo:
} arch_interrupt_frame_t;

typedef struct {
    bool enabled;
} arch_interrupt_state_t;

/**
 * @brief Gets the userspace instruction pointer from an interrupt register frame
 * @param regs The register frame to extract the instruction pointer from
 * @return The userspace instruction pointer
 */
[[nodiscard]] static inline uint64_t arch_interrupt_regs_syscall_get_user_ip(arch_interrupt_regs_t* regs) {
    (void) regs;
    return 0;
}

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
