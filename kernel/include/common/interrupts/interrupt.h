#pragma once
#if defined(__ARCH_X86_64__)
#include <arch/x86_64/interrupts/interrupt.h>
#elif defined(__ARCH_RISCV64__)
#include <arch/riscv64/interrupts/interrupt.h>
#else
#error "Unknown architecture"
#endif

#include <stdint.h>

typedef void (*interrupt_handler_fn_t)(arch_interrupt_frame_t* frame, void* ctx);

typedef struct dw_item dw_item_t; // NOLINT
typedef struct thread thread_t; // NOLINT

/**
 * @brief Initialize the interrupt system for the current core
 * @param core_id The ID of the current core
 */
void interrupt_init(uint32_t core_id);

/**
 * @brief Set the stack pointer to use when handling interrupts in user mode.
 * @param stack_pointer The stack pointer to use. This should point to the top of a valid stack in kernel space.
 */
void interrupt_set_usermode_stack(uint64_t stack_pointer);

/// @note: you must pick *only one* of the three options for handling interrupts
/// hardirq, softirq, or thread wake

/**
 * @brief Register an interrupt handler for the given interrupt vector.
 * @param handler The function to call when the specified interrupt is triggered.
 * @param ctx A context pointer that will be passed to the handler when it is called.
 * @note handler will run in a hardirq context
 */
void interrupt_set_hardirq_handler(uint8_t vector, interrupt_handler_fn_t handler, void* ctx);

/**
 * @brief Register an interrupt handler for the given interrupt vector.
 * @param dw_item The deferred work item to enqueue on interrupt
 * @note handler will run in a softirq context
 */
void interrupt_set_softirq_handler(uint8_t vector, dw_item_t* dw_item);

/**
 * @brief Register an interrupt handler for the given interrupt vector.
 * @param thread The thread to awake on interrupt
 */
void interrupt_set_thread_handler(uint8_t vector, thread_t* thread);
