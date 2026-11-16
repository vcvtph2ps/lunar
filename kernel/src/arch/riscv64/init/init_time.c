#include <arch/riscv64/hardware/time/rdtime.h>
#include <common/arch.h>
#include <common/assert.h>
#include <common/cpu_local.h>
#include <common/init.h>
#include <common/interrupts/dpc.h>
#include <common/interrupts/interrupt.h>
#include <common/log.h>
#include <common/time/time.h>

void init_stage_time(uint32_t core_id) {
    uint64_t timebase_freq = g_init_boot_info->riscv_timebase_frequency;
    if(timebase_freq == 0) {
        arch_panic("timebase frequency was not provided by the prekernel\n");
    }

    CPU_LOCAL_WRITE(timebase_frequency, timebase_freq);

    if(INIT_CORE_IS_BSP(core_id)) {
        time_timer_t* timer = arch_rdtime_timer_init();
        assert(timer != nullptr);

        time_init(timer);
    }
}
