#pragma once
#include <common/sync/spinlock.h>
#include <stdint.h>

typedef struct time_timer time_timer_t;

// @brief Sleeps for the specified number of microseconds.
typedef void (*time_sleep_fn_t)(time_timer_t* timer, uint64_t microseconds);

typedef uint64_t (*time_read_fn_t)(time_timer_t* self);

// @brief Calibrates the timer to a known reference. This is used to calibrate the timer to a known frequency or to a known time source.
typedef void (*time_calibrate_fn_t)(time_timer_t* self, time_timer_t* calibration_timer);

struct time_timer {
    // @brief The score of the timer. This is used to determine which timer is the best for timekeeping.
    uint32_t score;
    const char* name;

    // @note: these fields are nullable
    time_sleep_fn_t sleep;
    time_read_fn_t read_raw;
    time_read_fn_t read_microseconds;

    // @brief If true, this timer is not per-cpu and must be locked during busy sleep.
    bool exclusive;
    spinlock_no_int_t lock;

    void* private;
};

/**
 * @brief Returns the timer used for timekeeping. This is the timer that is used to implement time_monotonic_ns() and time_realtime_ns().
 */
time_timer_t* time_get_timekeeping_timer();

/**
 * @brief Initialises the timekeeping subsystem.
 * @note Must be called on the BSP after arch_tsc_calibrate() and after g_bootloader_info.boot_timestamp has been populated.
 */
void time_init(time_timer_t* timekeeping_timer);

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
