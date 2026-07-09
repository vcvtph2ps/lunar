#pragma once

#include <common/time/time.h>
#include <stdint.h>

/**
 * @brief Initializes the LAPIC timer for the current core and calibrates it using the provided calibration timer.
 * @param core_id The ID of the current core.
 * @param calibration_timer A pointer to a time_timer_t structure used for calibration.
 */
void arch_lapic_timer_init(uint32_t core_id, time_timer_t* calibration_timer);

/**
 * @brief Configures the LAPIC timer to fire a one-shot interrupt after the specified number of microseconds.
 * @param microseconds The number of microseconds to wait before firing the interrupt.
 */
void arch_lapic_timer_oneshot_us(uint32_t microseconds);

/**
 * @brief Configures the LAPIC timer to fire a one-shot interrupt after the specified number of milliseconds.
 * @param milliseconds The number of milliseconds to wait before firing the interrupt.
 */
void arch_lapic_timer_oneshot_ms(uint32_t milliseconds);

/**
 * @brief Stops any currently active LAPIC timer countdown, if there is one.
 */
void arch_lapic_timer_stop();
