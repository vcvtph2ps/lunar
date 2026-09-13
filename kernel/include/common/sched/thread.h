#pragma once
#include <common/interrupts/dw.h>
#include <common/sync/spinlock.h>
#include <lib/helpers.h>
#include <lib/list.h>
#include <lib/types.h>

typedef enum thread_state thread_state_t;
typedef struct thread thread_t;

typedef struct wait_queue wait_queue_t; // NOLINT
typedef struct process process_t; // NOLINT
typedef struct scheduler scheduler_t; // NOLINT

enum thread_state {
    THREAD_STATE_READY,
    THREAD_STATE_RUNNING,
    THREAD_STATE_BLOCKING,
    THREAD_STATE_BLOCKED,
    THREAD_STATE_DYING,
    THREAD_STATE_TERMINATED
};

struct thread {
    uint32_t tid;

    process_t* process;
    list_node_t list_node_process;

    struct {
        bool in_process;
        virt_addr_t address;
        dw_item_t dw_item;
    } vm_fault;

    struct {
        ATOMIC thread_state_t state;

        // The scheduler to which the thread belongs
        ATOMIC scheduler_t* owner;

        // Indicates whether the thread can be migrated to another scheduler
        ATOMIC bool migratable;

        ATOMIC bool in_run_queue;
        list_node_t run_queue_node;

        // The wait queue the thread is trying to enter when its block finalizes, if any
        wait_queue_t* wait_target;
        // The wait queue the thread is currently linked into, if any
        wait_queue_t* wait_queue;
        list_node_t wait_queue_node;
        list_node_t sleep_queue_node;

        // The time (in nanoseconds) until which the thread should sleep, 0 if untimed
        uint64_t sleep_until_ns;

        ATOMIC uint64_t wake_cookie;
        uint64_t sleep_cookie;
    } sched;

    bool in_interrupt_handler;
};
