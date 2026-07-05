#include <arch/hardware/fpu.h>
#include <arch/hardware/lapic.h>
#include <arch/hardware/pit.h>
#include <arch/internal/cpuid.h>
#include <arch/internal/gdt.h>
#include <arch/internal/tsc.h>
#include <common/arch.h>
#include <common/init.h>
#include <common/interrupts/dw.h>
#include <common/interrupts/interrupt.h>
#include <common/log.h>
#include <common/time/time.h>

void init_stage_time(uint32_t core_id) {
    time_calibrate_fn_t best_calibrator = arch_pit_sleep_us;
    // @todo: use the hpet if available
    arch_tsc_calibrate(best_calibrator);
    best_calibrator = arch_tsc_sleep;

    if(INIT_CORE_IS_BSP(core_id)) { time_init(); }
}
