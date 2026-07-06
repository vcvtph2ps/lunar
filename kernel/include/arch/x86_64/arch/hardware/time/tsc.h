#pragma once
#include <common/time/time.h>

/**
 * @brief Calibrates the TSC frequency and stores it in the per-CPU data structure. Must be called on each CPU during early initialisation.
 */
time_timer_t* arch_tsc_init_timer(time_timer_t* calibration_timer);

/**
 * @brief Returns the TSC timer instance for the current CPU.
 */
time_timer_t* arch_tsc_timer_get();

/**
 * @brief Reads the current value of the TSC.
 */
[[nodiscard]] static inline uint64_t arch_tsc_read() {
    return __rdtsc();
}
