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
    THREAD_STATE_DEAD
};

struct thread {
    spinlock_no_dw_t lock;

    uint32_t tid;
    thread_state_t state;
    scheduler_t* sched;

    bool migratable;

    list_node_t list_node_sched;
    list_node_t list_node_wait;
    list_node_t list_node_sleep_queue;

    uint64_t sleep_until; // The time (in nanoseconds) until which the thread should sleep
    wait_queue_t* current_wait_queue; // The wait queue the thread is currently in, if any
    wait_queue_t* target_wait_queue; // The wait queue the thread is trying to enter, if any

    bool in_interrupt_handler;
};
