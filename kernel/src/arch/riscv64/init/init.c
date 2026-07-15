#include <common/arch.h>
#include <common/init.h>
#include <common/log.h>
#include <lib/helpers.h>

#define INIT_STAGE(STAGE, HANDLER) { .stage = (STAGE), .handler = (HANDLER) }

init_stage_handler_t g_init_stage_handlers[] = {
    INIT_STAGE(INIT_STAGE_BASE_MEM, init_stage_base_mem),
    INIT_STAGE(INIT_STAGE_ARCH_CPU, init_stage_arch_cpu),
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
    run_stage(INIT_STAGE_BASE_MEM, 0);
    // run_stage(INIT_STAGE_ARCH_CPU, 0);
    arch_panic("mroaww");

    ATOMIC_LOAD_ADD(&g_init_finished_core_count, 1, ATOMIC_RELEASE);
    while(ATOMIC_LOAD(&g_init_finished_core_count, ATOMIC_ACQUIRE) != g_init_boot_info->core_count) { arch_spin_hint(); }

    while(1) { arch_spin_hint(); }
}

void arch_init_ap(uint32_t core_id) {
    while(ATOMIC_LOAD(&g_init_finished_core_count, ATOMIC_ACQUIRE) == 0) { arch_spin_hint(); }

    run_stage(INIT_STAGE_BASE_MEM, core_id);

    ATOMIC_LOAD_ADD(&g_init_finished_core_count, 1, ATOMIC_RELEASE);
    while(ATOMIC_LOAD(&g_init_finished_core_count, ATOMIC_ACQUIRE) != g_init_boot_info->core_count) { arch_spin_hint(); }

    while(1) { arch_spin_hint(); }
}
