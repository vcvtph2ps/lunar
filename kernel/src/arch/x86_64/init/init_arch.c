#include <arch/x86_64/hardware/fpu.h>
#include <arch/x86_64/hardware/ioapic.h>
#include <arch/x86_64/hardware/lapic.h>
#include <arch/x86_64/internal/cpuid.h>
#include <arch/x86_64/internal/cr.h>
#include <arch/x86_64/internal/gdt.h>
#include <arch/x86_64/internal/msr.h>
#include <arch/x86_64/interrupts/interrupt.h>
#include <common/arch.h>
#include <common/assert.h>
#include <common/cpu_local.h>
#include <common/init.h>
#include <common/interrupts/dpc.h>
#include <common/interrupts/interrupt.h>
#include <common/interrupts/ipi.h>
#include <common/log.h>
#include <uacpi/acpi.h>
#include <uacpi/event.h>
#include <uacpi/sleep.h>
#include <uacpi/status.h>
#include <uacpi/tables.h>
#include <uacpi/utilities.h>

static void setup_control_registers() {
    if(!arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_FXSR)) {
        arch_panic("x86_64: FXSR is not supported on this CPU\n");
    }

    // PE, MP, ET, WP, PG
    arch_cr_write_cr0(0x80010013);

    uint64_t cr4 = arch_cr_read_cr4();
    bool la57 = cr4 & (1 << 12);

    // PAE
    cr4 = 1u << 5; // CR4.PAE
    if(la57) {
        cr4 |= 1 << 12; // CR4.LA57
    }

    bool umip = arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_UMIP);
    bool smep = arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_SMEP);
    bool smap = arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_SMAP);
    bool pge = arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_GLOBAL_PAGES);

    if(umip) {
        cr4 |= 1u << 11; // CR4.UMIP
    }
    if(smep) {
        cr4 |= 1u << 20; // CR4.SMEP
    }
    if(smap) {
        cr4 |= 1u << 21; // CR4.SMAP
    }
    if(pge) {
        cr4 |= 1ul << 7; // CR4.PGE
    }

    arch_cr_write_cr4(cr4);

    if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_NX_PAGES)) {
        arch_msr_write(ARCH_MSR_EFER, arch_msr_read(ARCH_MSR_EFER) | (1u << 11)); // EFER.NXE
    }
}

void init_stage_arch_cpu(uint32_t core_id) {
    setup_control_registers();

    if(INIT_CORE_IS_BSP(core_id)) {
        if(arch_cpuid_get_hypervisor() != ARCH_CPUID_HYPERVISOR_NONE) {
            LOG_KTRC("Processor: %s, \"%s\" running under \"%s\"\n", arch_cpuid_get_vendor_string(), arch_cpuid_get_name_string(), arch_cpuid_get_hypervisor_string());
        } else {
            LOG_KTRC("Processor: %s, \"%s\"\n", arch_cpuid_get_vendor_string(), arch_cpuid_get_name_string());
        }

        LOG_KTRC("active_gs=0x%016lx\n", arch_msr_read(ARCH_MSR_ACTIVE_GS_BASE));
        LOG_INFO("x2apic: %s\n", arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_X2APIC) ? "supported" : "not supported");
    }

    dpc_init(core_id);
    arch_gdt_init();

    arch_cpu_local_set_gs_base(core_id);

    interrupt_init(core_id);
    arch_lapic_init(core_id);
    arch_fpu_init(core_id);
    ipi_init(core_id);
    CPU_LOCAL_WRITE(online, true);
}
