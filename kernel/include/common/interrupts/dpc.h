#pragma once

#include <common/sync/spinlock.h>
#include <lib/list.h>

typedef struct dpc dpc_t;

typedef void (*dpc_function_t)(void* data);
typedef void (*dpc_cleanup_fn_t)(dpc_t* item);

typedef struct {
    uintptr_t stack_pointer_top;
    uint32_t counter;

    spinlock_no_int_t queue_lock;
    list_t queue;
} dpc_queue_t;

typedef struct dpc {
    dpc_function_t fn;
    void* data;

    list_node_t list_node;

    ATOMIC dpc_queue_t* queue;

    /// @brief the target core to enqueue the dpc on, or UINT64_MAX to queue on the cpu that calls dpc_queue
    uint64_t target_core_id;
} dpc_t;

/**
 * @brief Initialize the DPC system. Must be called before using any other DPC functions.
 */
void dpc_init(uint32_t core_id);

/**
 * @brief Create a DPC.
 * @param fn The function to execute when the DPC is executed.
 * @param data The data to pass to the function when it is executed.
 */
dpc_t* dpc_create(dpc_function_t fn, void* data);

/**
 * @brief Queue a DPC.
 * @param dpc The dpc to queue.
 * @return true if the dpc was enqueued, otherwise false
 */
bool dpc_queue(dpc_t* dpc);

/// @brief Set the DPC's target core
void dpc_set_target_core(dpc_t* dpc, uint32_t target_core);

/**
 * @brief Process queued DPCs for the current cpu
 */
void dpc_process();

/**
 * @brief Disable DPC. (increments the status counter).
 */
void dpc_disable();

/**
 * @brief Enable DPC. (decrements the status counter).
 * @note DPC will only be processed when the status reaches zero
 */
void dpc_enable();
