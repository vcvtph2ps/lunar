#pragma once
#include <common/sync/wait_obj.h>
#include <lib/list.h>
#include <stdint.h>

typedef struct timer_wait {
    wait_obj_t obj;
    uint64_t deadline_ns;

    list_node_t list_node;

    ATOMIC bool pending;
} timer_wait_t;

/**
 * @brief Create a timer that will signal its wait object after the given number of milliseconds
 * @note The timer must be freed with timer_wait_free() once the wait it backs completes
 */
timer_wait_t* timer_wait_create(uint64_t msec);

/**
 * @brief Remove a timer from the global timer list and free it
 */
void timer_wait_free(timer_wait_t* timer);

/**
 * @brief Fire any timers whose deadline has passed, signalling their wait objects
 */
void timer_wait_check();
