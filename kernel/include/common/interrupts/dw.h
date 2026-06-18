#pragma once

#include <lib/list.h>

typedef struct dw_item dw_item_t;

typedef void (*dw_function_t)(void* data);
typedef void (*dw_cleanup_fn_t)(dw_item_t* item);

typedef struct dw_item {
    dw_function_t fn;
    dw_cleanup_fn_t cleanup_fn;
    void* data;
    list_node_t list_node;
    bool in_use;
} dw_item_t;

/**
 * @brief Initialize the deferred work system. Must be called before using any other deferred work functions.
 */
void dw_init();

/**
 * @brief Create an item of deferred work.
 * @param fn The function to execute when the item is processed.
 * @param data The data to pass to the function when it is executed.
 */
dw_item_t* dw_create(dw_function_t fn, void* data);

/**
 * @brief Queue an item of deferred work.
 * @param item The item to queue.
 */
void dw_queue(dw_item_t* item);

/**
 * @brief Process queued deferred work for the current cpu
 */
void dw_process();

/**
 * @brief Disable deferred work. (increments the status counter).
 */
void dw_status_disable();

/**
 * @brief Enable deferred work. (decrements the status counter).
 * @note Deferred work will only be processed when the status reaches zero
 */
void dw_status_enable();
