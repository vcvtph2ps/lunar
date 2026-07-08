#include <common/arch.h>
#include <common/init.h>

const char* init_stage_to_str(init_stage_t stage) {
    switch(stage) {
        case INIT_STAGE_BASE_MEM: return "base_mem";
        case INIT_STAGE_ARCH_CPU: return "arch_cpu";
        case INIT_STAGE_TIME:     return "time";
        case INIT_STAGE_SCHED:    return "sched";
    }
}
