#include <arch/cpu_local.h>
#include <arch/hardware/pit.h>
#include <arch/internal/tsc.h>
#include <common/log.h>
#include <common/time/time.h>
#include <stdint.h>

void arch_tsc_calibrate(time_calibrate_fn_t calibrate_fn) {
    uint64_t tsc_start = __rdtsc();
    calibrate_fn(10000);
    uint32_t tsc_ticks_per_us = (__rdtsc() - tsc_start) / 10000ull;
    LOG_OKAY("tsc calibrated: %d ticks/us\n", tsc_ticks_per_us);
    CPU_LOCAL_WRITE(tsc_ticks_per_us, tsc_ticks_per_us);
    __sync_synchronize();
}

void arch_tsc_sleep(uint64_t microseconds) {
    uint64_t tsc_ticks_per_us = CPU_LOCAL_READ(tsc_ticks_per_us);
    uint64_t tsc_start = __rdtsc();
    uint64_t tsc_end = tsc_start + (tsc_ticks_per_us * microseconds);
    while(__rdtsc() < tsc_end) { __asm__ volatile("pause"); }
}
