#include <arch/x86_64/cpu_local.h>
#include <arch/x86_64/hardware/time/tsc.h>
#include <common/log.h>
#include <common/time/time.h>
#include <memory/heap.h>
#include <stdint.h>

static time_timer_t* g_tsc_timer;

time_timer_t* arch_tsc_timer_get() {
    return g_tsc_timer;
}

static void arch_tsc_sleep(time_timer_t* self, uint64_t microseconds) {
    (void) self;
    uint64_t tsc_ticks_per_us = CPU_LOCAL_READ(tsc_ticks_per_us);
    uint64_t tsc_start = arch_tsc_read();
    uint64_t tsc_end = tsc_start + (tsc_ticks_per_us * microseconds);
    while(arch_tsc_read() < tsc_end) { __asm__ volatile("pause"); }
}

static uint64_t arch_tsc_read_raw(time_timer_t* self) {
    (void) self;
    return arch_tsc_read();
}

static uint64_t arch_tsc_read_us(time_timer_t* self) {
    (void) self;
    uint64_t tsc_value = arch_tsc_read();
    uint64_t tsc_ticks_per_us = CPU_LOCAL_READ(tsc_ticks_per_us);
    return tsc_value / tsc_ticks_per_us;
}

time_timer_t* arch_tsc_init_timer(time_timer_t* calibration_timer) {
    LOG_INFO("calibrating tsc using %s\n", calibration_timer->name);
    uint64_t tsc_start = arch_tsc_read();
    calibration_timer->sleep(calibration_timer, 10000);
    uint32_t tsc_ticks_per_us = (arch_tsc_read() - tsc_start) / 10000ull;
    LOG_OKAY("tsc calibrated: %d ticks/us\n", tsc_ticks_per_us);
    CPU_LOCAL_WRITE(tsc_ticks_per_us, tsc_ticks_per_us);

    if(g_tsc_timer == nullptr) {
        g_tsc_timer = (time_timer_t*) heap_alloc(sizeof(time_timer_t));
        g_tsc_timer->name = "tsc";
        g_tsc_timer->score = 50;
        g_tsc_timer->sleep = arch_tsc_sleep;
        g_tsc_timer->read_raw = arch_tsc_read_raw;
        g_tsc_timer->read_microseconds = arch_tsc_read_us;
        g_tsc_timer->private = nullptr;
    }

    return g_tsc_timer;
}
