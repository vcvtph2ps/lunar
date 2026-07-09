#pragma once
#include <common/sched/thread.h>
#include <common/sync/spinlock.h>
#include <lib/list.h>

#define WAIT_QUEUE_INIT ((wait_queue_t) { .lock = SPINLOCK_NO_DW_INIT, .list = LIST_INIT })

typedef struct wait_queue {
    spinlock_no_dw_t lock;
    list_t list;
} wait_queue_t;

/**
 * @brief Check if the wait queue is empty.
 * @note excludes threads that are not in the blocked state
 */
bool wait_queue_empty(wait_queue_t* queue);

/**
 * @brief Join the wait queue, blocking the calling thread until the queue is empty.
 * @param queue Pointer to the wait queue to join.
 */
void wait_queue_join(wait_queue_t* queue);

/**
 * @brief Join the wait queue, blocking the calling thread until the queue is empty.
 * @param queue Pointer to the wait queue to join.
 */
void wait_queue_add_thread(wait_queue_t* queue, thread_t* thread);

/**
 * @brief Pop a thread from the wait queue.
 * @param queue Pointer to the wait queue to pop from.
 * @return Pointer to the popped thread, or nullptr if the queue is empty.
 */
thread_t* wait_queue_pop(wait_queue_t* queue);
