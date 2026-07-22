#include <common/sched/sched.h>
#include <common/sync/wait_queue.h>
#include <lib/helpers.h>
#include <lib/list.h>


bool wait_queue_empty(wait_queue_t* queue) {
    spinlock_nodw_lock(&queue->lock);
    LIST_FOR_EACH(&queue->list, thread_node) {
        thread_t* thread = CONTAINER_OF(thread_node, thread_t, list_node_wait);
        spinlock_nodw_lock(&thread->lock);
        bool blocked = thread->state == THREAD_STATE_BLOCKED;
        spinlock_nodw_unlock(&thread->lock);
        if(blocked) {
            spinlock_nodw_unlock(&queue->lock);
            return false;
        }
    }
    spinlock_nodw_unlock(&queue->lock);
    return true;
}

void wait_queue_join(wait_queue_t* queue) {
    thread_t* current = sched_arch_thread_current();
    spinlock_nodw_lock(&current->lock);
    current->target_wait_queue = queue;
    spinlock_nodw_unlock(&current->lock);
    sched_yield(THREAD_STATE_BLOCKED);
}

void wait_queue_add_thread(wait_queue_t* queue, thread_t* thread) {
    spinlock_nodw_lock(&queue->lock);
    list_push(&queue->list, &thread->list_node_wait);
    spinlock_nodw_unlock(&queue->lock);
}

thread_t* wait_queue_pop(wait_queue_t* queue) {
    spinlock_nodw_lock(&queue->lock);

    LIST_FOR_EACH(&queue->list, thread_node) {
        thread_t* thread = CONTAINER_OF(thread_node, thread_t, list_node_wait);
        spinlock_nodw_lock(&thread->lock);
        bool blocked = thread->state == THREAD_STATE_BLOCKED;
        spinlock_nodw_unlock(&thread->lock);
        if(blocked) {
            list_node_delete(&queue->list, thread_node);
            spinlock_nodw_unlock(&queue->lock);
            return thread;
        }
    }

    spinlock_nodw_unlock(&queue->lock);
    return nullptr;
}
