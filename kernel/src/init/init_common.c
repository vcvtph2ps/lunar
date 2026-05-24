#include <common/arch.h>
#include <common/init.h>

const char* init_stage_to_str(init_stage_t stage) {
    switch(stage) {
        case INIT_STAGE_BASE_MEM: return "base_mem";
    }
}
