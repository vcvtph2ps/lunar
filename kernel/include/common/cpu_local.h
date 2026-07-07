#pragma once
#include <arch/cpu_local.h>

/**
 * @brief Initializes the CPU local storage for the BSP. This should only be called once by the BSP
 */
void cpu_local_init_bsp(uintptr_t cpu_local_ptr);

/*
 * @brief Initializes the CPU local storage for an AP. This should only be called once by each AP
 */
void cpu_local_init(uint32_t core_id);

/**
 * @brief Gets the CPU local storage for the given core ID
 */
arch_cpu_local_t* cpu_local_get_other(uint32_t core_id);
