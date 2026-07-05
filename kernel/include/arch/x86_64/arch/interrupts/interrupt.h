#pragma once
#include <stdint.h>

typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
} arch_interrupt_regs_t;

typedef struct {
    uint64_t rip, cs, rflags, rsp, ss;
} arch_interrupt_sregs_t;

typedef struct {
    uint64_t vector, error, interrupt_data;
    arch_interrupt_regs_t* regs;
    arch_interrupt_sregs_t* state;
    bool is_user;
} arch_interrupt_frame_t;

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
