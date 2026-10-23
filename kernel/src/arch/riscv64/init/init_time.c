#include <arch/riscv64/dt.h>
#include <common/arch.h>
#include <common/assert.h>
#include <common/cpu_local.h>
#include <common/init.h>
#include <common/interrupts/dpc.h>
#include <common/interrupts/interrupt.h>
#include <common/log.h>
#include <common/time/time.h>

void init_stage_time(uint32_t core_id) {
    (void) core_id;
    uint64_t timebase_freq = arch_dt_get_timebase_frequency();
    CPU_LOCAL_WRITE(timebase_frequency, timebase_freq);
}
