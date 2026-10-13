#pragma once
#include <common/interrupts/dw.h>
#include <common/sync/spinlock.h>
#include <common/sync/wait_obj.h>
#include <common/userspace/signal.h>
#include <lib/helpers.h>
#include <lib/list.h>
#include <lib/types.h>
#include <memory/vm.h>
#include <uapi/signal.h>

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

    /// Reference count of the thread. The thread is destroyed once it reaches zero
    ATOMIC uint32_t ref_count;

    process_t* process;
    list_node_t list_node_process;

    struct {
        bool in_process;
        vm_fault_reason_t reason;
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

    // The code the thread exited with
    ATOMIC int32_t exit_code;
    wait_obj_t thread_termination_object;

    ATOMIC bool in_reap_queue;
    list_node_t list_node_reap_queue;

    bool in_interrupt_handler;

    /// @brief Set when a signal arrives so a thread blocked in an interruptible wait bails out
    ATOMIC bool interrupted;

    struct {
        spinlock_t lock;
        uapi_signal_set_t masked_signal_set;
    } signal;

    signal_queue_t signal_queue;
};

/**
 * @brief Acquires a new reference to a thread.
 * @param thread The thread to reference
 * @return The same thread
 */
thread_t* thread_ref(thread_t* thread);

/**
 * @brief Releases a reference to a thread, destroying it once the last reference is dropped.
 * @param thread The thread to dereference
 */
void thread_deref(thread_t* thread);
