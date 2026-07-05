#pragma once
#include <common/time/time.h>

/**
 * @brief Calibrates the TSC frequency and stores it in the per-CPU data structure. Must be called on each CPU during early initialisation.
 */
void arch_tsc_calibrate(time_calibrate_fn_t calibrate_fn);

/**
 * @brief Sleeps for the specified number of microseconds using the TSC.
 */
void arch_tsc_sleep(uint64_t microseconds);
