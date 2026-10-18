#include <common/arch.h>
#include <common/cmdline.h>
#include <common/init.h>
#include <common/log.h>
#include <lib/string.h>

const char* init_stage_to_str(init_stage_t stage) {
    switch(stage) {
        case INIT_STAGE_BASE_MEM:       return "base_mem";
        case INIT_STAGE_ARCH_CPU:       return "arch_cpu";
        case INIT_STAGE_TIME:           return "time";
        case INIT_STAGE_SCHED:          return "sched";
        case INIT_STAGE_ACPI_EARLY:     return "acpi_early";
        case INIT_STAGE_PLATFORM_EARLY: return "platform_early";
        case INIT_STAGE_ACPI:           return "acpi";
        case INIT_STAGE_PLATFORM:       return "platform";
        case INIT_STAGE_VFS:            return "vfs";
        case INIT_STAGE_TTY:            return "tty";
        case INIT_STAGE_USERSPACE:      return "userspace";
    }
    return "unknown";
}

bool init_stage_from_str(const char* str, init_stage_t* stage) {
    if(str == nullptr || stage == nullptr) {
        return false;
    }

    for(int i = INIT_STAGE_BASE_MEM; i <= INIT_STAGE_USERSPACE; i++) {
        if(string_compare(str, init_stage_to_str((init_stage_t) i)) == 0) {
            *stage = (init_stage_t) i;
            return true;
        }
    }
    return false;
}

void init_stage_maybe_halt(init_stage_t stage, uint32_t core_id) {
    if(!INIT_CORE_IS_BSP(core_id)) {
        return;
    }

    init_stage_t stop_stage;
    if(!cmdline_get_stop_at_stage(&stop_stage) || stop_stage != stage) {
        return;
    }

    LOG_INFO("halting BSP at stage %s (%i)\n", init_stage_to_str(stage), stage);

    for(;;) {
        (void) arch_interrupt_disable();
        arch_wait_for_interrupt();
    }
}
