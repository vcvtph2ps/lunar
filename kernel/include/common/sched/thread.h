#pragma once
#include <common/interrupts/dw.h>
#include <common/sync/spinlock.h>
#include <common/sync/wait_obj.h>
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

    THREAD_STATE_WAITING_IN_PROGRESS,
    THREAD_STATE_WAITING_ABORTED,
    THREAD_STATE_WAITING,

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

        wait_entry_t* wait_entry;
    } sched;

    bool in_interrupt_handler;
};
