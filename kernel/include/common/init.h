#include <lib/helpers.h>
#include <stdbool.h>
#include <stdint.h>

#include "protocol/bootinfo.h"

typedef enum {
    INIT_STAGE_BASE_MEM
} init_stage_t;

typedef struct {
    init_stage_t stage;
    void (*handler)(uint32_t core_id);
} init_stage_handler_t;

extern bootinfo_t* g_init_boot_info;

const char* init_stage_to_str(init_stage_t stage);

#define INIT_CORE_IS_BSP(CORE_ID) ((CORE_ID) == 0)

void init_stage_base_mem(uint32_t core_id);
