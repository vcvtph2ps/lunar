#pragma once
#include <common/time/time.h>

/**
 * @brief Creates the riscv rdtime timer
 * @return The timer instance, or nullptr if the timebase frequency is unknown
 */
time_timer_t* arch_rdtime_timer_init(void);


/**
 * @brief Reads the current value of the time csr
 */
[[nodiscard]] static inline uint64_t arch_rdtime_read() {
    uint64_t now;
    asm volatile("rdtime %0" : "=r"(now));
    return now;
}
