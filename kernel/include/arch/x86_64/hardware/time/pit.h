#pragma once
#include <common/time/time.h>
#include <stdint.h>

/**
 * @brief Returns the PIT timer instance.
 * @note Will also initialize the PIT timer if it has not been initialized yet.
 */
time_timer_t* arch_pit_timer_get();
