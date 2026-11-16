#include <arch/riscv64/hardware/plic.h>
#include <common/arch.h>
#include <common/hardware/uart16550.h>
#include <common/init.h>
#include <common/log.h>
#include <uacpi/status.h>
#include <uacpi/uacpi.h>

void init_stage_acpi_early(uint32_t core_id) {
    if(INIT_CORE_IS_BSP(core_id) && g_init_boot_info->rdsp_physical) {
        uacpi_status status = uacpi_initialize(UACPI_FLAG_BAD_CSUM_FATAL);
        if(uacpi_unlikely_error(status)) {
            LOG_FAIL("ACPI: early initialization failed uacpi_initialize, %s\n", uacpi_status_to_string(status));
        }
    }
}

void init_stage_platform_early(uint32_t core_id) {
    if(INIT_CORE_IS_BSP(core_id)) {
        arch_plic_init();
    }
}

void init_stage_platform(uint32_t core_id) {
    if(INIT_CORE_IS_BSP(core_id)) {
        uart16550_setup();
    }
}
