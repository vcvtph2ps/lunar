#pragma once
#include <arch/memory.h>
#include <stdint.h>

/**
 * @brief Hint to the processor that we are in a tight loop and it should relax
 */
void arch_spin_hint();

/**
 * @brief Waits for the next interrupt
 */
void arch_wait_for_interrupt();

/**
 * @brief Gets the ID of the current core
 */
uint64_t arch_get_core_id();

/**
 * @brief Panics the system with a formatted message
 * @param format The printf-style format string
 * @param ... The arguments for the format string
 * @note This function does not return and will halt the system after printing the message
 */
[[noreturn, gnu::format(printf, 1, 2)]] void arch_panic(const char* format, ...);

/**
 * @brief Initializes the bootstrap processor and starts the kernel
 * @note This function does not return and will transfer control to the kernel main function
 */
[[noreturn]] void arch_init_bsp();


/**
 * @brief Initializes an application processor and starts it
 * @param core_id The ID of the core to initialize
 * @note This function does not return and will transfer control to the kernel main function
 */
[[noreturn]] void arch_init_ap(uint32_t core_id);
