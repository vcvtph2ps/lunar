#pragma once

#include <lib/helpers.h>
#include <protocol/bootinfo.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    INIT_STAGE_BASE_MEM,
    INIT_STAGE_ARCH_CPU,
    INIT_STAGE_TIME,
    INIT_STAGE_SCHED,
    INIT_STAGE_ACPI_EARLY,
    INIT_STAGE_PLATFORM_EARLY,
    INIT_STAGE_ACPI,
    INIT_STAGE_PLATFORM
} init_stage_t;

typedef struct {
    init_stage_t stage;
    void (*handler)(uint32_t core_id);
} init_stage_handler_t;

extern bootinfo_t* g_init_boot_info;

const char* init_stage_to_str(init_stage_t stage);

#define INIT_CORE_IS_BSP(CORE_ID) ((CORE_ID) == 0)
#define INIT_CORE_IS_AP(CORE_ID) ((CORE_ID) != 0)

void init_stage_base_mem(uint32_t core_id);
void init_stage_arch_cpu(uint32_t core_id);
void init_stage_time(uint32_t core_id);
void init_stage_acpi_early(uint32_t core_id);
void init_stage_platform_early(uint32_t core_id);
void init_stage_acpi(uint32_t core_id);
void init_stage_platform(uint32_t core_id);
