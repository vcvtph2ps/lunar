#pragma once
#include <common/time/time.h>
#include <stdint.h>

/**
 * @brief Initializes the kvm pvclock.
 * @returns A pointer to the kvm pvclock timer instance, or nullptr
 */
time_timer_t* arch_kvm_pvclock_init();
