#include <arch/riscv64/hardware/fpu.h>
#include <common/cpu_local.h>
#include <common/init.h>
#include <common/interrupts/dpc.h>
#include <common/interrupts/interrupt.h>
#include <common/interrupts/ipi.h>

void init_stage_arch_cpu(uint32_t core_id) {
    arch_fpu_init(core_id);

    interrupt_init(core_id);
    dpc_init(core_id);
    ipi_init(core_id);
    CPU_LOCAL_WRITE(online, true);
}
