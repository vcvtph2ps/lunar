#pragma once
#include <stdint.h>

// @brief Function pointer type for a function that must sleep for @p microseconds
typedef void (*time_calibrate_fn_t)(uint64_t microseconds);

/**
 * @brief Initialises the timekeeping subsystem.
 * @note Must be called on the BSP after arch_tsc_calibrate() and after g_bootloader_info.boot_timestamp has been populated.
 */
void time_init();

/**
 * @brief Returns the number of nanoseconds elapsed since time_init() was called.
 * The value is strictly monotonically increasing and is never affected by realtime clock adjustments.
 * @return Nanoseconds since boot (monotonic).
 */
uint64_t time_monotonic_ns();

/**
 * @brief Returns the current wall-clock time as nanoseconds since the UNIX epoch.
 * @return Nanoseconds since the UNIX epoch (realtime).
 */
uint64_t time_realtime_ns();

/**
 * @brief Synchronises the realtime clock to an external reference.
 *
 * After this call, time_realtime_ns() will return values anchored to @p unix_ns at the moment of the call. Monotonic time is unaffected.
 * @param unix_ns Current wall-clock time expressed as nanoseconds since the UNIX epoch
 */
void time_sync_realtime(uint64_t unix_ns);
