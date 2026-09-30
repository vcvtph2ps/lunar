#pragma once
#include <common/ldr/ldr.h>
#include <common/sync/spinlock.h>
#include <common/sync/wait_obj.h>
#include <common/userspace/fd_store.h>
#include <lib/list.h>
#include <memory/vm.h>
#include <stdint.h>
#include <uapi/signal.h>

typedef struct process process_t;
typedef struct thread thread_t; // NOLINT

typedef enum {
    PROCESS_RUNNING,
    PROCESS_EXITED,
    PROCESS_TERMINATED,
    PROCESS_PENDING_REAP
} process_status_state_t;

typedef struct {
    spinlock_no_dw_t lock;
    process_status_state_t state;

    union {
        struct {
            int32_t exit_code;
        } exited;
        struct {
            int32_t signal_number;
        } terminated;
    };
} process_status_t;

struct process {
    uint32_t process_id;
    uint32_t parent_process_id;

    process_status_t status;

    /// The process that forked this one, nullptr for the init process
    process_t* parent;

    /// Node of this process in its parent's children_list
    list_node_t list_node_sibling;
    spinlock_no_dw_t children_lock;
    list_t children_list;

    /// Signaled whenever one of this process' children transitions to a zombie
    wait_obj_t child_exit_event;

    spinlock_no_dw_t thread_list_lock;
    list_t thread_list;

    vm_address_space_t* address_space;
    vfs_node_t* current_working_dir;
    fd_store_t* fd_store;

    /// Signal handlers set by sigaction
    struct {
        spinlock_t lock;
        uapi_signal_handler_t handlers[UAPI_SIGNAL_MAX_SIGNAL + 1];
    } signal_handler;
};

/**
 * @brief Allocates a new process ID.
 * @return A unique process ID.
 */
uint32_t process_allocate_id();

/**
 * @brief Frees a previously allocated process ID.
 * @param id The process ID to free.
 */
void process_free_id(uint32_t id);

/**
 * @brief Creates a process from the file and arguments
 */
process_t* process_create_from_file(vfs_node_t* node, const ldr_process_load_info_t* load_info, vfs_node_t* current_working_dir, thread_t** out_thread);

/**
 * @brief Creates a process from the file and arguments
 */
process_t* process_fork(process_t* parent);

/**
 * @brief Kills the target process
 * @param target The process to kill
 * @note This function never returns if it is called from a thread of the target process
 */
void process_kill(process_t* target, process_status_state_t state, int32_t exit_code);
