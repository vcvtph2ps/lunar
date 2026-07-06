#include <arch/cpu_local.h>
#include <common/init.h>
#include <common/log.h>
#include <common/time/time.h>
#include <stdint.h>

static time_timer_t* g_time_timekeeping_timer;

static uint64_t g_init_time_value;
static uint32_t g_tsc_ticks_per_us;

static uint64_t g_realtime_base_ns;
static uint64_t g_realtime_mono_ref_ns;

time_timer_t* time_get_timekeeping_timer() {
    return g_time_timekeeping_timer;
}

void time_init(time_timer_t* timekeeping_timer) {
    LOG_INFO("initialising timekeeping using %s\n", timekeeping_timer->name);
    g_time_timekeeping_timer = timekeeping_timer;
    g_init_time_value = g_time_timekeeping_timer->read_microseconds(g_time_timekeeping_timer);
    g_tsc_ticks_per_us = CPU_LOCAL_READ(tsc_ticks_per_us);

    g_realtime_base_ns = g_init_boot_info->boot_timestamp * 1000000000ULL;
    g_realtime_mono_ref_ns = 0;

    LOG_OKAY("monotonic clock initialised (init_time_value=%llu)\n", (unsigned long long) g_init_time_value);
    LOG_OKAY("realtime seeded from boot_timestamp=%llu\n", (unsigned long long) g_init_boot_info->boot_timestamp);
}

uint64_t time_monotonic_ns() {
    uint64_t time_value_now = g_time_timekeeping_timer->read_microseconds(g_time_timekeeping_timer);
    uint64_t elapsed_ticks = time_value_now - g_init_time_value;
    return (elapsed_ticks / g_tsc_ticks_per_us) * 1000ULL;
}

uint64_t time_realtime_ns() {
    uint64_t mono = time_monotonic_ns();
    return g_realtime_base_ns + (mono - g_realtime_mono_ref_ns);
}

void time_sync_realtime(uint64_t unix_ns) {
    g_realtime_mono_ref_ns = time_monotonic_ns();
    g_realtime_base_ns = unix_ns;
    LOG_OKAY("realtime synced to %llu ns\n", (unsigned long long) unix_ns);
}
