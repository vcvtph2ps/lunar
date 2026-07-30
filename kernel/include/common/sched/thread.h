#pragma once
#include <common/sync/spinlock.h>
#include <lib/helpers.h>
#include <lib/list.h>
#include <lib/types.h>

typedef enum thread_state thread_state_t;
typedef struct thread thread_t;

typedef struct wait_queue wait_queue_t; // NOLINT
typedef struct scheduler scheduler_t; // NOLINT

enum thread_state {
    THREAD_STATE_READY,
    THREAD_STATE_RUNNING,
    THREAD_STATE_BLOCKED,
    THREAD_STATE_DYING,
    THREAD_STATE_DEAD
};

struct thread {
    uint32_t tid;

    ATOMIC thread_state_t current_state;

    // The scheduler to which the thread belongs
    ATOMIC scheduler_t* sched;

    // Indicates whether the thread can be migrated to another scheduler
    ATOMIC bool migratable;

    // Weather the thread is currently in a run queue
    ATOMIC bool in_run_queue;

    // Indicates whether a thread was woken while running
    ATOMIC bool wake_pending;

    // Node for the scheduler's run queue
    list_node_t list_node_sched;
    // Node for the wait queue's thread list
    list_node_t list_node_wait;
    // Node for the sleep queue's thread list
    list_node_t list_node_sleep_queue;

    // The time (in nanoseconds) until which the thread should sleep
    uint64_t sleep_until;
    // The wait queue the thread is currently in, if any
    wait_queue_t* current_wait_queue;
    // The wait queue the thread is trying to enter, if any
    wait_queue_t* target_wait_queue;

    bool in_interrupt_handler;
};
