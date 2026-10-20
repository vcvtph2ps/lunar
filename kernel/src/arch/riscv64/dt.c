#include <arch/riscv64/dt.h>
#include <common/init.h>
#include <common/libfdt.h>
#include <common/log.h>
#include <memory/ptm.h>

uint64_t arch_dt_get_timebase_frequency(void) {
    if(g_init_boot_info == nullptr || g_init_boot_info->dtb_physical == 0) {
        LOG_WARN("no DTB provided in boot info\n");
        return 0;
    }

    const void* fdt = PTM_TO_HHDM(g_init_boot_info->dtb_physical);

    int rc = fdt_check_header(fdt);
    if(rc != 0) {
        LOG_WARN("invalid FDT header at 0x%lx (err=%d)\n", (unsigned long) g_init_boot_info->dtb_physical, rc);
        return 0;
    }

    int cpus_node = fdt_path_offset(fdt, "/cpus");
    if(cpus_node < 0) {
        LOG_WARN("/cpus node not found (err=%d)\n", cpus_node);
        return 0;
    }

    int len = 0;
    const fdt32_t* prop = (const fdt32_t*) fdt_getprop(fdt, cpus_node, "timebase-frequency", &len);
    if(prop == nullptr || len < (int) sizeof(fdt32_t)) {
        LOG_WARN("timebase-frequency property not found in /cpus node\n");
        return 0;
    }

    uint64_t frequency = fdt32_to_cpu(*prop);
    LOG_INFO("timebase-frequency = %llu Hz\n", (unsigned long long) frequency);
    return frequency;
}
