#include <common/sched/sched.h>
#include <common/sched/sleep_queue.h>
#include <common/time/time.h>
#include <lib/list.h>

void sleep_queue_insert(sleep_queue_t* queue, thread_t* item) {
    spinlock_nodw_lock(&queue->lock);

    // If the sleep queue is empty, just add the item
    if(queue->queue.count == 0) {
        list_push_back(&queue->queue, &item->list_node_sleep_queue);
        spinlock_nodw_unlock(&queue->lock);
        return;
    }

    // Otherwise, look through the sleep queue and find and insert in the correct position
    list_node_t* node = queue->queue.head;
    while(node) {
        thread_t* current = CONTAINER_OF(node, thread_t, list_node_sleep_queue);
        if(item->sleep_until < current->sleep_until) {
            list_node_prepend(&queue->queue, node, &item->list_node_sleep_queue);
            spinlock_nodw_unlock(&queue->lock);
            return;
        }
        node = node->next;
    }

    // the thread has the largest sleep_until
    list_push_back(&queue->queue, &item->list_node_sleep_queue);
    spinlock_nodw_unlock(&queue->lock);
}

void sleep_queue_check(sleep_queue_t* queue) {
    spinlock_nodw_lock(&queue->lock);

    if(queue->queue.count == 0) {
        spinlock_nodw_unlock(&queue->lock);
        return;
    }

    uint64_t current_time = time_monotonic_ns();
    LIST_FOR_EACH(&queue->queue, node) {
        thread_t* current = CONTAINER_OF(node, thread_t, list_node_sleep_queue);
        if(current->sleep_until > current_time) break;

        list_node_delete(&queue->queue, &current->list_node_sleep_queue);
        current->sleep_until = 0;
        current->target_wait_queue = nullptr;

        sched_thread_schedule(current);
    }
    spinlock_nodw_unlock(&queue->lock);
}
