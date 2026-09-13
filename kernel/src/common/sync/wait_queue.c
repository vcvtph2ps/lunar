#include <common/sched/sched.h>
#include <common/sched/sleep_queue.h>
#include <common/sync/wait_queue.h>
#include <common/time/time.h>
#include <lib/helpers.h>
#include <lib/list.h>

extern sleep_queue_t g_sched_sleep_queue;

bool wait_queue_empty(wait_queue_t* queue) {
    spinlock_nodw_lock(&queue->lock);
    LIST_FOR_EACH(&queue->list, thread_node) {
        thread_t* thread = CONTAINER_OF(thread_node, thread_t, sched.wait_queue_node);
        if(ATOMIC_LOAD(&thread->sched.state, ATOMIC_SEQ_CST) == THREAD_STATE_BLOCKED) {
            spinlock_nodw_unlock(&queue->lock);
            return false;
        }
    }
    spinlock_nodw_unlock(&queue->lock);
    return true;
}

void wait_queue_join(wait_queue_t* queue) {
    thread_t* current = sched_arch_thread_current();
    current->sched.wait_target = queue;
    ATOMIC_STORE(&current->sched.sleep_cookie, ATOMIC_LOAD(&current->sched.wake_cookie, ATOMIC_RELAXED), ATOMIC_RELAXED);
    sched_yield(THREAD_STATE_BLOCKING);
}

void wait_queue_join_timeout(wait_queue_t* queue, uint64_t timeout_ms) {
    thread_t* current = sched_arch_thread_current();
    current->sched.wait_target = queue;
    current->sched.sleep_until_ns = time_monotonic_ns() + (timeout_ms * 1000000ULL);
    ATOMIC_STORE(&current->sched.sleep_cookie, ATOMIC_LOAD(&current->sched.wake_cookie, ATOMIC_RELAXED), ATOMIC_RELAXED);
    sched_yield(THREAD_STATE_BLOCKING);
}

void wait_queue_add_thread(wait_queue_t* queue, thread_t* thread) {
    spinlock_nodw_lock(&queue->lock);
    list_push(&queue->list, &thread->sched.wait_queue_node);
    thread->sched.wait_queue = queue;
    spinlock_nodw_unlock(&queue->lock);
}

thread_t* wait_queue_pop(wait_queue_t* queue) {
    spinlock_nodw_lock(&queue->lock);

    LIST_FOR_EACH(&queue->list, thread_node) {
        thread_t* thread = CONTAINER_OF(thread_node, thread_t, sched.wait_queue_node);
        if(ATOMIC_LOAD(&thread->sched.state, ATOMIC_SEQ_CST) == THREAD_STATE_BLOCKED) {
            list_node_delete(&queue->list, thread_node);
            thread->sched.wait_queue = nullptr;
            spinlock_nodw_unlock(&queue->lock);

            if(ATOMIC_LOAD(&thread->sched.sleep_until_ns, ATOMIC_RELAXED) != 0) {
                ATOMIC_STORE(&thread->sched.sleep_until_ns, 0, ATOMIC_RELAXED);
                spinlock_nodw_lock(&g_sched_sleep_queue.lock);
                list_node_delete(&g_sched_sleep_queue.queue, &thread->sched.sleep_queue_node);
                spinlock_nodw_unlock(&g_sched_sleep_queue.lock);
            }

            return thread;
        }
    }

    spinlock_nodw_unlock(&queue->lock);
    return nullptr;
}

bool wait_queue_wake_one(wait_queue_t* queue) {
    thread_t* thread = wait_queue_pop(queue);
    if(thread == nullptr) return false;
    sched_thread_schedule(thread);
    return true;
}

size_t wait_queue_wake_all(wait_queue_t* queue) {
    size_t count = 0;
    while(wait_queue_wake_one(queue)) count++;
    return count;
}
