#pragma once
#include <stdint.h>

/**
 * @brief Initializes the PIT. Only used for lapic calibration
 */
void arch_pit_init(void);

/**
 * @brief Blocks the current execution for the specified number of microseconds using the PIT
 * @param microseconds The number of microseconds to sleep
 */
void arch_pit_sleep_us(uint64_t microseconds);
