#include <arch/x86_64/hardware/fpu.h>
#include <arch/x86_64/hardware/ioapic.h>
#include <arch/x86_64/hardware/lapic.h>
#include <arch/x86_64/internal/cpuid.h>
#include <arch/x86_64/internal/cr.h>
#include <arch/x86_64/internal/gdt.h>
#include <arch/x86_64/internal/msr.h>
#include <arch/x86_64/interrupts/interrupt.h>
#include <common/arch.h>
#include <common/init.h>
#include <common/interrupts/dw.h>
#include <common/interrupts/interrupt.h>
#include <common/interrupts/ipi.h>
#include <common/log.h>
#include <uacpi/acpi.h>
#include <uacpi/event.h>
#include <uacpi/sleep.h>
#include <uacpi/status.h>
#include <uacpi/tables.h>
#include <uacpi/utilities.h>

void init_stage_arch_cpu(uint32_t core_id) {
    if(INIT_CORE_IS_BSP(core_id)) {
        if(arch_cpuid_get_hypervisor() != ARCH_CPUID_HYPERVISOR_NONE) {
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


void init_stage_acpi_early(uint32_t core_id) {
    if(INIT_CORE_IS_BSP(core_id)) {
        uacpi_status status = uacpi_initialize(UACPI_FLAG_BAD_CSUM_FATAL);
        if(uacpi_unlikely_error(status)) { arch_panic("ACPI: early initialization failed uacpi_initialize, %s\n", uacpi_status_to_string(status)); }
    }
}

void init_stage_platform_early(uint32_t core_id) {
    if(INIT_CORE_IS_BSP(core_id)) { arch_ioapic_init(); }
}

void init_stage_acpi(uint32_t core_id) {
    if(INIT_CORE_IS_BSP(core_id)) {
        uacpi_status status = uacpi_namespace_load();
        if(uacpi_unlikely_error(status)) { arch_panic("ACPI: initialization failed uacpi_namespace_load, %s\n", uacpi_status_to_string(status)); }

        status = uacpi_set_interrupt_model(UACPI_INTERRUPT_MODEL_IOAPIC);
        if(uacpi_unlikely_error(status)) { arch_panic("ACPI: initialization failed uacpi_set_interrupt_model, %s\n", uacpi_status_to_string(status)); }

        status = uacpi_namespace_initialize();
        if(uacpi_unlikely_error(status)) { arch_panic("ACPI: initialization failed uacpi_namespace_initialize, %s\n", uacpi_status_to_string(status)); }

        status = uacpi_finalize_gpe_initialization();
        if(uacpi_unlikely_error(status)) { arch_panic("ACPI: initialization failed uacpi_finalize_gpe_initialization, %s\n", uacpi_status_to_string(status)); }
    }
}


static uacpi_interrupt_ret power_btn(uacpi_handle ctx) {
    (void) ctx;
    LOG_OKAY("power button pressed\n");

    uacpi_status status = uacpi_prepare_for_sleep_state(UACPI_SLEEP_STATE_S5);
    if(status != UACPI_STATUS_OK) {
        LOG_WARN("ACPI: Failed to prepare for S5: %s\n", uacpi_status_to_string(status));
        return UACPI_INTERRUPT_NOT_HANDLED;
    }

    (void) arch_interrupt_disable();
    LOG_INFO("ACPI: Entering S5...\n");

    status = uacpi_enter_sleep_state(UACPI_SLEEP_STATE_S5);
    if(uacpi_unlikely_error(status)) {
        LOG_WARN("ACPI: Failed to enter S5: %s\n", uacpi_status_to_string(status));
        return UACPI_INTERRUPT_NOT_HANDLED;
    }

    return UACPI_INTERRUPT_HANDLED;
}

void init_stage_platform(uint32_t core_id) {
    if(INIT_CORE_IS_BSP(core_id)) {
        uacpi_status status = uacpi_install_fixed_event_handler(UACPI_FIXED_EVENT_POWER_BUTTON, power_btn, nullptr);
        if(uacpi_unlikely_error(status)) { arch_panic("ACPI: initialization failed uacpi_install_fixed_event_handler, %s\n", uacpi_status_to_string(status)); }

        status = uacpi_enable_fixed_event(UACPI_FIXED_EVENT_POWER_BUTTON);
        if(uacpi_unlikely_error(status)) { arch_panic("ACPI: initialization failed uacpi_enable_fixed_event, %s\n", uacpi_status_to_string(status)); }
    }
}
