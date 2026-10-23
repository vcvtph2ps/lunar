#pragma once

#include <lib/list.h>

typedef struct dpc dpc_t;

typedef void (*dpc_function_t)(void* data);
typedef void (*dpc_cleanup_fn_t)(dpc_t* item);

typedef struct dpc {
    dpc_function_t fn;
    dpc_cleanup_fn_t cleanup_fn;
    void* data;
    list_node_t list_node;

    ATOMIC bool in_use;
} dpc_t;

/**
 * @brief Initialize the deferred work system. Must be called before using any other deferred work functions.
 */
void dpc_init();

/**
 * @brief Create an item of deferred work.
 * @param fn The function to execute when the item is processed.
 * @param data The data to pass to the function when it is executed.
 */
dpc_t* dpc_create(dpc_function_t fn, void* data);

/**
 * @brief Queue an item of deferred work.
 * @param item The item to queue.
 */
void dpc_queue(dpc_t* item);

/**
 * @brief Process queued deferred work for the current cpu
 */
void dpc_process();

/**
 * @brief Disable deferred work. (increments the status counter).
 */
void dpc_disable();

/**
 * @brief Enable deferred work. (decrements the status counter).
 * @note Deferred work will only be processed when the status reaches zero
 */
void dpc_enable();
