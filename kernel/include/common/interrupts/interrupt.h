#pragma once
#if defined(__ARCH_X86_64__)
#include <arch/x86_64/interrupts/interrupt.h>
#elif defined(__ARCH_RISCV64__)
#include <arch/riscv64/interrupts/interrupt.h>
#else
#error "Unknown architecture"
#endif

#include <stdint.h>

typedef void (*interrupt_handler_fn_t)(arch_interrupt_frame_t* frame);

/**
 * @brief Initialize the interrupt system for the current core
 * @param core_id The ID of the current core
 */
void interrupt_init(uint32_t core_id);

/**
 * @brief Register an interrupt handler for the given interrupt vector.
 * @param vector The interrupt vector to register the handler for.
 * @param handler The function to call when the specified interrupt is triggered.
 * @note handler will run in a hardirq context
 */
void interrupt_set_handler(uint8_t vector, interrupt_handler_fn_t handler);

/**
 * @brief Set the stack pointer to use when handling interrupts in user mode.
 * @param stack_pointer The stack pointer to use. This should point to the top of a valid stack in kernel space.
 */
void interrupt_set_usermode_stack(uint64_t stack_pointer);
