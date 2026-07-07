#include <arch/internal/cpuid.h>
#include <arch/internal/cr.h>
#include <arch/internal/msr.h>
#include <common/arch.h>
#include <common/init.h>
#include <common/log.h>
#include <lib/helpers.h>
#include <stddef.h>

#include "arch/interrupts/interrupt.h"
#include "common/interrupts/dw.h"
#include "common/sched/sched.h"

#define INIT_STAGE(STAGE, HANDLER) { .stage = (STAGE), .handler = (HANDLER) }

init_stage_handler_t g_init_stage_handlers[] = {
    INIT_STAGE(INIT_STAGE_BASE_MEM, init_stage_base_mem),
    INIT_STAGE(INIT_STAGE_ARCH_CPU, init_stage_arch_cpu),
    INIT_STAGE(INIT_STAGE_TIME, init_stage_time),
};

#undef INIT_STAGE

static void run_stage(init_stage_t stage, uint32_t core_id) {
    for(size_t i = 0; i < sizeof(g_init_stage_handlers) / sizeof(init_stage_handler_t); i++) {
        if(g_init_stage_handlers[i].stage == stage) {
            g_init_stage_handlers[i].handler(core_id);
            LOG_OKAY("finished stage %s (%i) on %s\n", init_stage_to_str(stage), stage, INIT_CORE_IS_BSP(core_id) ? "bsp" : "ap");
            return;
        }
    }
    LOG_WARN("No handler found for stage %s (%i)\n", init_stage_to_str(stage), stage);
}

ATOMIC uint32_t g_init_finished_core_count = 0;

void arch_init_bsp() {
    if(arch_cpuid_get_vendor_string() != nullptr) {
        LOG_STRC("Processor: %s, \"%s\" running under \"%s\"\n", arch_cpuid_get_vendor_string(), arch_cpuid_get_name_string(), arch_cpuid_get_hypervisor_string());
    } else {
        LOG_STRC("Processor: %s, \"s%s\"\n", arch_cpuid_get_vendor_string(), arch_cpuid_get_name_string());
    }

    LOG_STRC("cr0=0x%016lx\n", arch_cr_read_cr0());
    LOG_STRC("cr4=0x%016lx\n", arch_cr_read_cr4());
    LOG_STRC("xcr0=0x%016lx\n", arch_cr_read_xcr0());
    LOG_STRC("efer=0x%016lx\n", arch_msr_read(ARCH_MSR_EFER));
    LOG_STRC("active_gs=0x%016lx\n", arch_msr_read(ARCH_MSR_ACTIVE_GS_BASE));

    if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_XSAVE)) {
        LOG_INFO("fpu save method: xsave, fpu_size=%d\n", arch_cpuid(0x0d, 0, ARCH_CPUID_ECX));
    } else {
        LOG_INFO("fpu save method: fxsave, fpu_size=512\n");
    }

    LOG_INFO("x2apic: %s\n", arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_X2APIC) ? "supported" : "not supported");

    run_stage(INIT_STAGE_BASE_MEM, 0);
    run_stage(INIT_STAGE_ARCH_CPU, 0);
    run_stage(INIT_STAGE_TIME, 0);

    // let APs know they can start init, and wait for them
    ATOMIC_LOAD_ADD(&g_init_finished_core_count, 1, ATOMIC_RELEASE);
    while(ATOMIC_LOAD(&g_init_finished_core_count, ATOMIC_ACQUIRE) != g_init_boot_info->core_count) { arch_spin_hint(); }

    (void) arch_interrupt_enable();
    dw_status_enable();
    sched_preempt_enable();

    arch_panic("mroaww");
    while(1);
}

void arch_init_ap(uint32_t core_id) {
    // Wait for BSP to finish init
    while(ATOMIC_LOAD(&g_init_finished_core_count, ATOMIC_ACQUIRE) == 0) { arch_spin_hint(); }

    LOG_STRC("cr0=0x%016lx\n", arch_cr_read_cr0());
    LOG_STRC("cr4=0x%016lx\n", arch_cr_read_cr4());
    LOG_STRC("xcr0=0x%016lx\n", arch_cr_read_xcr0());
    LOG_STRC("efer=0x%016lx\n", arch_msr_read(ARCH_MSR_EFER));
    LOG_STRC("active_gs=0x%016lx\n", arch_msr_read(ARCH_MSR_ACTIVE_GS_BASE));

    run_stage(INIT_STAGE_BASE_MEM, core_id);
    run_stage(INIT_STAGE_ARCH_CPU, core_id);
    run_stage(INIT_STAGE_TIME, core_id);

    // signal that this core has finished init
    ATOMIC_LOAD_ADD(&g_init_finished_core_count, 1, ATOMIC_RELEASE);
    while(ATOMIC_LOAD(&g_init_finished_core_count, ATOMIC_ACQUIRE) != g_init_boot_info->core_count) { arch_spin_hint(); }

    (void) arch_interrupt_enable();
    dw_status_enable();
    sched_preempt_enable();

    while(1);
}
