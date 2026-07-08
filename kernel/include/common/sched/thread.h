#pragma once
#include <lib/list.h>
#include <lib/types.h>

typedef enum thread_state thread_state_t;
typedef struct thread thread_t;
typedef struct scheduler scheduler_t; // NOLINT

enum thread_state {
    THREAD_STATE_READY,
    THREAD_STATE_RUNNING,
    THREAD_STATE_BLOCKED,
    THREAD_STATE_DEAD
};

struct thread {
    uint32_t tid;
    thread_state_t state;
    scheduler_t* sched;

    list_node_t list_node_sched;
    bool in_interrupt_handler;
};
