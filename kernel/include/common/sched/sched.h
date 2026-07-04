#pragma once
#include <common/spinlock.h>

typedef struct scheduler scheduler_t; // NOLINT

struct scheduler {
    ATOMIC bool yield_pending;
    ATOMIC uint32_t preempt_counter;
    bool threaded;
};

/**
 * @brief Increments the preemption counter
 */
void sched_preempt_disable();

/**
 * @brief Decrements the preemption counter
 * @note If the counter reaches zero and a yield is pending, the current thread will yield to the scheduler before returning.
 */
void sched_preempt_enable();
