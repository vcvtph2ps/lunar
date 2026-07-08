#include <arch/cpu_local.h>
#include <arch/hardware/lapic.h>
#include <arch/hardware/lapic_timer.h>
#include <arch/internal/cpuid.h>
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


void arch_lapic_timer_init(uint32_t core_id, time_timer_t* calibration_timer) {
    LOG_INFO("calibrating lapic timer using %s\n", calibration_timer->name);

    arch_lapic_write(LAPIC_TIMER_DIV, 0x3);
    arch_lapic_write(LAPIC_LVT_TIMER, 0x20);

    if(!INIT_CORE_IS_BSP(core_id)) { return; }

    arch_lapic_write(LAPIC_TIMER_INIT_COUNT, 0xFFFFFFFF);
    calibration_timer->sleep(calibration_timer, 10000);
    uint32_t elapsed = 0xFFFFFFFF - arch_lapic_read(LAPIC_TIMER_CUR_COUNT);
    arch_lapic_timer_stop();

    CPU_LOCAL_WRITE(lapic_timer_ticks_per_us, (uint32_t) (elapsed / 10000ull));

    LOG_OKAY("apic timer calibrated: %d ticks/us\n", CPU_LOCAL_READ(lapic_timer_ticks_per_us));
}

void arch_lapic_timer_oneshot_us(uint32_t microseconds) {
    uint32_t ticks = (uint32_t) (microseconds * CPU_LOCAL_READ(lapic_timer_ticks_per_us));
    arch_lapic_write(LAPIC_TIMER_INIT_COUNT, ticks);
}

void arch_lapic_timer_oneshot_ms(uint32_t milliseconds) {
    arch_lapic_timer_oneshot_us(milliseconds * 1000);
}

void arch_lapic_timer_stop() {
    arch_lapic_write(LAPIC_TIMER_INIT_COUNT, 0);
}
