#include <arch/hardware/fpu.h>
#include <arch/hardware/lapic.h>
#include <arch/internal/cpuid.h>
#include <arch/internal/gdt.h>
#include <common/arch.h>
#include <common/init.h>
#include <common/interrupts/dw.h>
#include <common/interrupts/interrupt.h>
#include <common/interrupts/ipi.h>
#include <common/log.h>

void init_stage_arch_cpu(uint32_t core_id) {
    if(INIT_CORE_IS_BSP(core_id)) { dw_init(); }

    arch_gdt_init();
    interrupt_init(core_id);
    arch_lapic_init(core_id);
    arch_fpu_init(core_id);
    ipi_init(core_id);
}
