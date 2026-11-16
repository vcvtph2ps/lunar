#pragma once

#include <common/interrupts/interrupt.h>
#include <stdbool.h>
#include <stdint.h>

#define ARCH_PLIC_MAX_IRQS 1024

/// @brief Discovers and initializes the PLIC
void arch_plic_init(void);

/// @brief Whether the PLIC was successfully initialized
[[nodiscard]] bool arch_plic_is_initialized(void);

/**
 * @brief translate a ACPI GSI into a PLIC interrupt source id
 * @param gsi The global system interrupt number
 * @return The PLIC interrupt source id, or 0 if it is out of range
 */
[[nodiscard]] uint32_t arch_plic_gsi_to_irq(uint32_t gsi);

/**
 * @brief Registers the handler invoked when the given interrupt source fires
 * @param irq The PLIC interrupt source id
 * @param handler The handler to invoke, or nullptr to clear it
 * @param ctx Context passed to the handler
 */
void arch_plic_set_handler(uint32_t irq, interrupt_handler_fn_t handler, void* ctx);

/**
 * @brief Sets the priority of an interrupt source
 * @param irq The PLIC interrupt source id
 * @param priority The priority to assign, 0 disables the source
 */
void arch_plic_set_priority(uint32_t irq, uint32_t priority);

/**
 * @brief Enables an interrupt source
 * @param irq The PLIC interrupt source id
 * @param priority The priority to assign
 */
void arch_plic_enable_irq(uint32_t irq, uint32_t priority);

/**
 * @brief Disables an interrupt source, clearing its priority and enable bits
 * @param irq The PLIC interrupt source id
 */
void arch_plic_disable_irq(uint32_t irq);

/**
 * @brief Claims the highest priority pending interrupt for the current hart
 * @return The claimed interrupt source id, or 0 if there is nothing to claim
 */
[[nodiscard]] uint32_t arch_plic_claim(void);

/**
 * @brief Signals completion of an interrupt source to the PLIC
 * @param irq The interrupt source id previously returned by @ref arch_plic_claim
 */
void arch_plic_complete(uint32_t irq);
