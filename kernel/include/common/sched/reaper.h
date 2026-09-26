#pragma once
#include <common/sched/thread.h>
#include <stddef.h>

void reaper_init();

/**
 * @brief Queues a terminated thread for the reaper thread to process
 * @param thread The terminated thread
 * @note The thread must be in the THREAD_STATE_TERMINATED state and must not be on any run queue
 * @note The reaper is woken up, so this may be called from any thread context
 */
void reaper_queue_thread(thread_t* thread);

/**
 * @brief The number of terminated threads that are waiting to be processed by the reaper
 */
size_t reaper_pending_count();
