#pragma once
#include <common/ldr/ldr.h>
#include <common/sync/spinlock.h>
#include <common/sync/wait_obj.h>
#include <common/userspace/fd_store.h>
#include <common/userspace/signal.h>
#include <lib/list.h>
#include <memory/vm.h>
#include <stdint.h>
#include <uapi/signal.h>

typedef struct process process_t;
typedef struct thread thread_t; // NOLINT
typedef struct process_group process_group_t; // NOLINT

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

    /// Reference count of the process. The process is destroyed once it reaches zero
    ATOMIC uint32_t ref_count;

    process_status_t status;

    /// The process that forked this one, nullptr for the init process
    process_t* parent;

    /// The list node for the global process list
    list_node_t list_node_global;

    /// Whether list_node_global is currently linked into the global process list
    bool in_global_list;

    /// The process group this process belongs to, nullptr until it is placed in one
    process_group_t* group;
    /// Node of this process in its group's members list
    list_node_t list_node_group;

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

    signal_queue_t signal_queue;
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
 * @note: bumps process refcount
 */
process_t* process_find_by_id(uint32_t id);

/**
 * @brief Looks up a thread of a process by its tid.
 * @return A referenced thread, or nullptr
 * @note The caller owns the returned reference and must release it with thread_deref()
 */
thread_t* process_find_thread_by_id(process_t* process, uint32_t tid);

/**
 * @brief Creates a process from the file and arguments
 */
process_t* process_create_from_file(vfs_node_t* node, const ldr_process_load_info_t* load_info, vfs_node_t* current_working_dir, thread_t** out_thread);

/**
 *
 */
bool process_exec_from_file(process_t* current, vfs_node_t* node, const ldr_process_load_info_t* load_info, thread_t** out_thread);

/**
 * @brief Creates a process from the parent process
 */
process_t* process_fork(process_t* parent);

/**
 * @brief Kills the target process
 * @param target The process to kill
 * @note This function never returns if it is called from a thread of the target process
 */
void process_kill(process_t* target, process_status_state_t state, int32_t exit_code);

/**
 * @brief Acquires a new reference to a process.
 * @param process The process to reference
 * @return The same process
 */
process_t* process_ref(process_t* process);

/**
 * @brief Acquires a new reference to a process only if it is still alive.
 * @param process The process to reference
 * @return The same process, or nullptr if it is already being destroyed
 */
process_t* process_try_ref(process_t* process);

/**
 * @brief Releases a reference to a process, destroying it once the last reference is dropped.
 * @param process The process to dereference
 */
void process_deref(process_t* process);
