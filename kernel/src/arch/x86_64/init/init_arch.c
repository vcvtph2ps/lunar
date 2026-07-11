#include <arch/x86_64/hardware/fpu.h>
#include <arch/x86_64/hardware/lapic.h>
#include <arch/x86_64/internal/cpuid.h>
#include <arch/x86_64/internal/cr.h>
#include <arch/x86_64/internal/gdt.h>
#include <arch/x86_64/internal/msr.h>
#include <common/arch.h>
#include <common/init.h>
#include <common/interrupts/dw.h>
#include <common/interrupts/interrupt.h>
#include <common/interrupts/ipi.h>
#include <common/log.h>

void init_stage_arch_cpu(uint32_t core_id) {
    if(INIT_CORE_IS_BSP(core_id)) {
        if(arch_cpuid_get_vendor_string() != nullptr) {
            LOG_STRC("Processor: %s, \"%s\" running under \"%s\"\n", arch_cpuid_get_vendor_string(), arch_cpuid_get_name_string(), arch_cpuid_get_hypervisor_string());
        } else {
            LOG_STRC("Processor: %s, \"s%s\"\n", arch_cpuid_get_vendor_string(), arch_cpuid_get_name_string());
        }

        LOG_STRC("cr0=0x%016lx\n", arch_cr_read_cr0());
        LOG_STRC("cr4=0x%016lx\n", arch_cr_read_cr4());
        if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_XSAVE)) { LOG_STRC("xcr0=0x%016lx\n", arch_cr_read_xcr0()); }
        LOG_STRC("efer=0x%016lx\n", arch_msr_read(ARCH_MSR_EFER));
        LOG_STRC("active_gs=0x%016lx\n", arch_msr_read(ARCH_MSR_ACTIVE_GS_BASE));

        if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_XSAVE)) {
            LOG_INFO("fpu save method: xsave, fpu_size=%d\n", arch_cpuid(0x0d, 0, ARCH_CPUID_ECX));
        } else {
            LOG_INFO("fpu save method: fxsave, fpu_size=512\n");
        }

        LOG_INFO("x2apic: %s\n", arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_X2APIC) ? "supported" : "not supported");
    }

    if(INIT_CORE_IS_BSP(core_id)) { dw_init(); }

    arch_gdt_init();
    interrupt_init(core_id);
    arch_lapic_init(core_id);
    arch_fpu_init(core_id);
    ipi_init(core_id);
}
