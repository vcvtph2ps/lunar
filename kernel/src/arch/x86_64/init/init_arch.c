#include <arch/internal/cpuid.h>
#include <arch/internal/gdt.h>
#include <common/init.h>
#include <common/interrupts/dw.h>
#include <common/log.h>

void init_stage_arch_cpu(uint32_t core_id) {
    if(INIT_CORE_IS_BSP(core_id)) { dw_init(); }

    arch_gdt_init();
}
