#include <common/sched/sched.h>
#include <common/sched/sleep_queue.h>
#include <common/time/time.h>
#include <lib/list.h>
#include <lib/string.h>

#define SLEEP_QUEUE_WAKE_BATCH 16

void sleep_queue_insert(sleep_queue_t* queue, thread_t* item) {
    spinlock_nodw_lock(&queue->lock);

    // If the sleep queue is empty, just add the item
    if(queue->queue.count == 0) {
        list_push_back(&queue->queue, &item->sched.sleep_queue_node);
        spinlock_nodw_unlock(&queue->lock);
        return;
    }

    // Otherwise, look through the sleep queue and find and insert in the correct position
    list_node_t* node = queue->queue.head;
    while(node) {
        thread_t* current = CONTAINER_OF(node, thread_t, sched.sleep_queue_node);
        if(item->sched.sleep_until_ns < current->sched.sleep_until_ns) {
            list_node_prepend(&queue->queue, node, &item->sched.sleep_queue_node);
            spinlock_nodw_unlock(&queue->lock);
            return;
        }
        node = node->next;
    }

    // the thread has the largest sleep_until
    list_push_back(&queue->queue, &item->sched.sleep_queue_node);
    spinlock_nodw_unlock(&queue->lock);
}

void sleep_queue_check(sleep_queue_t* queue) {
    thread_t* to_wake[SLEEP_QUEUE_WAKE_BATCH];
    size_t wake_count = 0;

    spinlock_nodw_lock(&queue->lock);

    if(queue->queue.count == 0) {
        spinlock_nodw_unlock(&queue->lock);
        return;
    }

    uint64_t current_time = time_monotonic_ns();
    LIST_FOR_EACH(&queue->queue, node) {
        thread_t* current = CONTAINER_OF(node, thread_t, sched.sleep_queue_node);
        if(current->sched.sleep_until_ns > current_time) break;

        thread_state_t state = ATOMIC_LOAD(&current->sched.state, ATOMIC_ACQUIRE);
        if(state != THREAD_STATE_BLOCKED) continue;

        list_node_delete(&queue->queue, &current->sched.sleep_queue_node);
        current->sched.sleep_until_ns = 0;
        current->sched.wait_target = nullptr;

        if(wake_count < SLEEP_QUEUE_WAKE_BATCH) {
            to_wake[wake_count++] = current;
        }
    }
    spinlock_nodw_unlock(&queue->lock);

    for(size_t i = 0; i < wake_count; i++) {
        sched_thread_schedule(to_wake[i]);
    }
}
