#include <arch/x86_64/hardware/fpu.h>
#include <arch/x86_64/hardware/ioapic.h>
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
#include <uacpi/acpi.h>
#include <uacpi/event.h>
#include <uacpi/tables.h>
#include <uacpi/utilities.h>

void init_stage_arch_cpu(uint32_t core_id) {
    if(INIT_CORE_IS_BSP(core_id)) {
        if(arch_cpuid_get_vendor_string() != nullptr) {
            LOG_STRC("Processor: %s, \"%s\" running under \"%s\"\n", arch_cpuid_get_vendor_string(), arch_cpuid_get_name_string(), arch_cpuid_get_hypervisor_string());
        } else {
            LOG_STRC("Processor: %s, \"%s\"\n", arch_cpuid_get_vendor_string(), arch_cpuid_get_name_string());
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

#define UACPI_CHECK_STATUS(fn, x)                                                                                          \
    if((x) != UACPI_STATUS_OK) { arch_panic("ACPI initialization failed: " #fn " %s\n", uacpi_status_to_string(status)); }


void init_stage_acpi_early(uint32_t core_id) {
    if(INIT_CORE_IS_BSP(core_id)) {
        uacpi_status status = uacpi_initialize(UACPI_FLAG_BAD_CSUM_FATAL);
        UACPI_CHECK_STATUS(uacpi_initialize, status);
    }
}

void init_stage_platform_early(uint32_t core_id) {
    if(INIT_CORE_IS_BSP(core_id)) { arch_ioapic_init(); }
}

void init_stage_acpi(uint32_t core_id) {
    if(INIT_CORE_IS_BSP(core_id)) {
        uacpi_status status = uacpi_namespace_load();
        UACPI_CHECK_STATUS(uacpi_namespace_load, status);

        status = uacpi_set_interrupt_model(UACPI_INTERRUPT_MODEL_IOAPIC);
        UACPI_CHECK_STATUS(uacpi_set_interrupt_model, status);

        status = uacpi_namespace_initialize();
        UACPI_CHECK_STATUS(uacpi_namespace_initialize, status);

        status = uacpi_finalize_gpe_initialization();
        UACPI_CHECK_STATUS(uacpi_finalize_gpe_initialization, status);
    }
}


static uacpi_interrupt_ret power_btn(uacpi_handle ctx) {
    (void) ctx;

    LOG_INFO("beep\n");
    return UACPI_INTERRUPT_HANDLED;
}

void init_stage_platform(uint32_t core_id) {
    if(INIT_CORE_IS_BSP(core_id)) {
        uacpi_status status = uacpi_install_fixed_event_handler(UACPI_FIXED_EVENT_POWER_BUTTON, power_btn, nullptr);
        UACPI_CHECK_STATUS(uacpi_enable_fixed_event, status);

        status = uacpi_enable_fixed_event(UACPI_FIXED_EVENT_POWER_BUTTON);
        UACPI_CHECK_STATUS(uacpi_enable_fixed_event, status);
    }
}
