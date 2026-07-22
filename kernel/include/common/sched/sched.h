#pragma once
#include <common/sched/thread.h>
#include <common/sync/spinlock.h>
#include <common/sync/wait_queue.h>
#include <lib/list.h>

typedef struct scheduler scheduler_t; // NOLINT

struct scheduler {
    ATOMIC bool yield_pending;
    ATOMIC uint32_t preempt_counter;
    bool threaded;

    spinlock_t lock;
    list_t thread_queue;
    thread_t* idle_thread;
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

/**
 * @brief Creates a new kernel thread with the specified entry point
 * @param entry_point The virtual address where the thread should begin execution
 * @return A pointer to the newly created thread structure
 */
thread_t* sched_arch_create_kernel_thread(virt_addr_t entry_point);

/**
 * @brief Adds a thread to the scheduler's run queue
 * @param thread The thread to schedule
 */
void sched_thread_schedule(thread_t* thread);

/**
 * @brief Yields the current thread's execution and transitions to the specified state
 * @param state The new state for the current thread
 */
void sched_yield(thread_state_t state);

/**
 * @brief Puts the current thread to sleep for the specified duration in milliseconds
 * @param msec The duration to sleep in milliseconds
 */
void sched_sleep(uint64_t msec);

/**
 * @brief Initializes the scheduler for the current core
 * @param core_id The ID of the current core
 */
void sched_init(uint32_t core_id);

/**
 * @brief Returns the currently running thread on the current CPU
 * @return A pointer to the currently executing thread, or NULL if none
 */
thread_t* sched_arch_thread_current();

/**
 * @brief Switches execution context from the current thread to the next thread
 * @param t_current The currently running thread
 * @param t_next The thread to switch to
 * @param yield_state The state to set for the previous thread after switching
 */
void sched_arch_context_switch(thread_t* t_current, thread_t* t_next, thread_state_t yield_state);

/**
 * @brief Resets the preemption timer for the current CPU
 */
void sched_arch_reset_preempt_timer();

/**
 * @brief Hands off execution to the scheduler for the first time on the current CPU\
 */
[[noreturn]] void sched_arch_handoff();
