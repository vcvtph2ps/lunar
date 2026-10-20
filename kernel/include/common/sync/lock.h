#pragma once

#include <common/sync/spinlock.h>
#include <common/sync/wait_obj.h>
#include <lib/list.h>

#define LOCK_INIT ((lock_t) { .state = LOCK_STATE_UNLOCKED, .lock = SPINLOCK_NO_INT_INIT, .wait_obj = WAIT_OBJ_INIT_SYNCHRONIZATION() })

typedef enum {
    LOCK_STATE_UNLOCKED,
    LOCK_STATE_LOCKED,
    LOCK_STATE_CONTESTED
} lock_state_t;

typedef struct {
    spinlock_no_int_t lock;
    lock_state_t state;

    ATOMIC size_t waiters;
    wait_obj_t wait_obj;
} lock_t;

/**
 * @brief Acquire lock.
 * @param lock Pointer to the lock to acquire.
 * @note This function will block the calling thread until the lock is acquired.
 */
void lock_acquire(lock_t* lock);

/**
 * @brief Release lock.
 * @param lock Pointer to the lock to release.
 * @note This function will wake up one of the threads waiting on the lock, if any.
 */
void lock_release(lock_t* lock);
