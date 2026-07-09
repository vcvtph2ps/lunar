#pragma once

#include <common/sync/spinlock.h>
#include <common/sync/wait_queue.h>
#include <lib/list.h>

#define MUTEX_INIT ((mutex_t) { .state = MUTEX_STATE_UNLOCKED, .lock = SPINLOCK_NO_INT_INIT, .wait_queue = WAIT_QUEUE_INIT })

typedef enum {
    MUTEX_STATE_UNLOCKED,
    MUTEX_STATE_LOCKED,
    MUTEX_STATE_CONTESTED
} mutex_state_t;

typedef struct {
    spinlock_no_int_t lock;
    mutex_state_t state;
    wait_queue_t wait_queue;
} mutex_t;

/**
 * @brief Acquire mutex.
 * @param mutex Pointer to the mutex to acquire.
 * @note This function will block the calling thread until the mutex is acquired.
 */
void mutex_acquire(mutex_t* mutex);

/**
 * @brief Release mutex.
 * @param mutex Pointer to the mutex to release.
 * @note This function will wake up one of the threads waiting on the mutex, if any.
 */
void mutex_release(mutex_t* mutex);
