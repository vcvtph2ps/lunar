#pragma once

/**
 * @brief Increments the preemption counter
 */
void sched_preempt_disable();

/**
 * @brief Decrements the preemption counter
 * @note If the counter reaches zero and a yield is pending, the current thread will yield to the scheduler before returning.
 */
void sched_preempt_enable();
