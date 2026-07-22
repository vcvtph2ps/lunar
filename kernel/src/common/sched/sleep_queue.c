#include <common/sched/sched.h>
#include <common/sched/sleep_queue.h>
#include <common/time/time.h>
#include <lib/list.h>

void sleep_queue_insert(sleep_queue_t* queue, thread_t* item) {
    spinlock_nodw_lock(&queue->lock);

    // If the wait queue is empty, just add the item
    if(queue->wait_queue.count == 0) {
        list_push_back(&queue->wait_queue, &item->list_node_sleep_queue);
        spinlock_nodw_unlock(&queue->lock);
        return;
    }

    // Otherwise, look through the wait queue and find and insert in the correct position
    list_node_t* node = queue->wait_queue.head;
    while(node) {
        thread_t* current = CONTAINER_OF(node, thread_t, list_node_sleep_queue);
        if(item->sleep_until < current->sleep_until) {
            list_node_prepend(&queue->wait_queue, node, &item->list_node_sleep_queue);
            spinlock_nodw_unlock(&queue->lock);
            return;
        }
    }

    // the thread has the largest sleep_until
    list_push_back(&queue->wait_queue, &item->list_node_sleep_queue);
    spinlock_nodw_unlock(&queue->lock);
}

void sleep_queue_check(sleep_queue_t* queue) {
    spinlock_nodw_lock(&queue->lock);

    if(queue->wait_queue.count == 0) {
        spinlock_nodw_unlock(&queue->lock);
        return;
    }

    uint64_t current_time = time_monotonic_ns();
    LIST_FOR_EACH(&queue->wait_queue, node) {
        thread_t* current = CONTAINER_OF(node, thread_t, list_node_sleep_queue);
        if(current->sleep_until > current_time) {
            // Since the queue is sorted, we can stop checking once we find a thread that hasn't timed out yet
            break;
        }

        spinlock_nodw_lock(&current->lock);
        // the thread may have been woken.
        if(current->state != THREAD_STATE_BLOCKED) {
            spinlock_nodw_unlock(&current->lock);
            continue;
        }

        list_node_delete(&queue->wait_queue, &current->list_node_sleep_queue);
        // if the thread is in a wait queue, remove it from that wait queue as well, since we hit the timeout
        if(current->current_wait_queue) {
            list_node_delete(&current->current_wait_queue->list, &current->list_node_wait);
            current->current_wait_queue = nullptr;
        }

        current->sleep_until = 0;
        current->target_wait_queue = nullptr;
        current->current_wait_queue = nullptr;

        sched_thread_schedule(current);
        spinlock_nodw_unlock(&current->lock);
    }
    spinlock_nodw_unlock(&queue->lock);
}
