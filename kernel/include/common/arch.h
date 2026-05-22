#pragma once
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
 *
 */
uint64_t arch_get_core_id();

/**
 *
 */
[[noreturn, gnu::format(printf, 1, 2)]] void arch_panic(const char* format, ...);
