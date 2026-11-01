#include <arch/x86_64/hardware/lapic.h>
#include <arch/x86_64/hardware/lapic_timer.h>
#include <arch/x86_64/internal/cpuid.h>
#include <common/cpu_local.h>
#include <common/init.h>
#include <common/log.h>
#include <common/time/time.h>
#include <memory/vm.h>
#include <stdint.h>

#define LAPIC_TIMER_INIT_COUNT 0x380
#define LAPIC_TIMER_CUR_COUNT 0x390
#define LAPIC_TIMER_DIV 0x3E0
#define LAPIC_LVT_TIMER 0x320
#define LAPIC_MASK_MASKED 0x10000

#define LAPIC_CALIBRATION_US 50000

static uint64_t g_lapic_ticks_per_us = 0;

void arch_lapic_timer_calibrate(time_timer_t* calibration_timer) {
    LOG_INFO("calibrating lapic timer on using %s\n", calibration_timer->name);

    arch_lapic_write(LAPIC_TIMER_INIT_COUNT, 0xFFFFFFFF);
    calibration_timer->sleep(calibration_timer, LAPIC_CALIBRATION_US);
    uint32_t elapsed = 0xFFFFFFFF - arch_lapic_read(LAPIC_TIMER_CUR_COUNT);
    arch_lapic_timer_stop();

    uint32_t ticks_per_us = (uint32_t) (((uint64_t) elapsed + (LAPIC_CALIBRATION_US / 2)) / LAPIC_CALIBRATION_US);
    g_lapic_ticks_per_us = ticks_per_us;

    LOG_OKAY("apic timer calibrated: %ld ticks/us\n", g_lapic_ticks_per_us);
}

void arch_lapic_timer_init(uint32_t core_id) {
    (void) core_id;
    arch_lapic_write(LAPIC_TIMER_DIV, 0xB); // divide by 1
    arch_lapic_write(LAPIC_LVT_TIMER, 0x20);
}

void arch_lapic_timer_oneshot_us(uint32_t microseconds) {
    uint32_t ticks = (uint32_t) (microseconds * g_lapic_ticks_per_us);
    arch_lapic_write(LAPIC_TIMER_INIT_COUNT, ticks);
}

void arch_lapic_timer_oneshot_ms(uint32_t milliseconds) {
    arch_lapic_timer_oneshot_us(milliseconds * 1000);
}

void arch_lapic_timer_stop() {
    arch_lapic_write(LAPIC_TIMER_INIT_COUNT, 0);
}
