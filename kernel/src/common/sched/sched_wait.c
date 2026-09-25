#include <common/interrupts/interrupt.h>
#include <common/sched/sched.h>
#include <common/sched/thread.h>
#include <common/sched/timer_wait.h>
#include <common/sync/spinlock.h>
#include <common/sync/wait_obj.h>
#include <lib/helpers.h>
#include <lib/list.h>
#include <memory/heap.h>

void sched_wait_single(wait_obj_t* obj, uint64_t timeout_ms) {
    sched_wait_multiple(&obj, 1, WAIT_OBJ_WAIT_ANY, timeout_ms);
}

static void sched_wait_abort() {
    thread_t* thread = sched_arch_thread_current();
    spinlock_lock(&thread->sched.wait_entry->lock);
    while(true) {
        list_node_t* node = list_pop(&thread->sched.wait_entry->wait_blocks_list);
        if(node == nullptr) {
            break;
        }
        wait_block_t* wb = CONTAINER_OF(node, wait_block_t, thread_node);
        spinlock_nodw_lock(&wb->obj->lock);
        list_node_delete(&wb->obj->wait_blocks, &wb->wait_object_node);
        spinlock_nodw_unlock(&wb->obj->lock);
        heap_free(wb, sizeof(wait_block_t));
    }
    spinlock_unlock(&thread->sched.wait_entry->lock);
    thread->sched.wait_entry = nullptr;
}

static bool sched_wait_any(wait_obj_t** objects, size_t count) {
    thread_t* thread = sched_arch_thread_current();

    for(size_t i = 0; i < count; i++) {
        wait_obj_t* obj = objects[i];

        spinlock_nodw_lock(&obj->lock);

        if(wait_obj_try_consume(obj)) {
            spinlock_nodw_unlock(&obj->lock);
            return true;
        }

        wait_block_t* wb = heap_alloc(sizeof(wait_block_t));
        wb->obj = obj;
        wb->entry = thread->sched.wait_entry;
        ATOMIC_STORE(&wb->signaled, false, ATOMIC_RELAXED);

        spinlock_lock(&thread->sched.wait_entry->lock);
        list_push_back(&thread->sched.wait_entry->wait_blocks_list, &wb->thread_node);
        spinlock_unlock(&thread->sched.wait_entry->lock);

        list_push_back(&obj->wait_blocks, &wb->wait_object_node);

        spinlock_nodw_unlock(&obj->lock);
    }
    return false;
}

static void sort_wait_objects(void** objects, size_t n) {
    for(size_t i = 1; i < n; ++i) {
        void* x = objects[i];
        uintptr_t key = (uintptr_t) x;
        size_t j = i;

        while(j > 0 && (uintptr_t) objects[j - 1] > key) {
            objects[j] = objects[j - 1];
            --j;
        }

        objects[j] = x;
    }
}

static bool sched_wait_all(wait_obj_t** objects, size_t count) {
    thread_t* thread = sched_arch_thread_current();

    /// We do this to ensure lock ordering
    sort_wait_objects((void**) objects, count);

    /// Lock every object
    for(size_t i = 0; i < count; i++) {
        wait_obj_t* obj = objects[i];
        spinlock_nodw_lock(&obj->lock);
    }

    /// Look at every object, if all are signaled we can skip wait blocks and consume
    bool abort_wait = true;
    for(size_t i = 0; i < count; i++) {
        wait_obj_t* obj = objects[i];
        if(!wait_obj_is_signaled(obj)) {
            abort_wait = false;
        }
    }

    /// Look at every object, if they are all signaled, consume, else add wait block
    for(size_t i = 0; i < count; i++) {
        wait_obj_t* obj = objects[i];
        if(abort_wait) {
            bool status = wait_obj_try_consume(obj);
            assert(status && "Wait object changed state while locked...");
        } else {
            wait_block_t* wb = heap_alloc(sizeof(wait_block_t));
            wb->obj = obj;
            wb->entry = thread->sched.wait_entry;
            ATOMIC_STORE(&wb->signaled, false, ATOMIC_RELAXED);

            spinlock_lock(&thread->sched.wait_entry->lock);
            list_push_back(&thread->sched.wait_entry->wait_blocks_list, &wb->thread_node);
            spinlock_unlock(&thread->sched.wait_entry->lock);

            list_push_back(&obj->wait_blocks, &wb->wait_object_node);
        }
    }

    /// Unlock every object
    for(size_t i = 0; i < count; i++) {
        wait_obj_t* obj = objects[i];
        spinlock_nodw_unlock(&obj->lock);
    }

    return abort_wait;
}

static bool sched_wait_on_timer(timer_wait_t* timer) {
    thread_t* thread = sched_arch_thread_current();

    spinlock_nodw_lock(&timer->obj.lock);

    if(wait_obj_try_consume(&timer->obj)) {
        spinlock_nodw_unlock(&timer->obj.lock);
        return true;
    }

    wait_block_t* wb = heap_alloc(sizeof(wait_block_t));
    wb->obj = &timer->obj;
    wb->entry = thread->sched.wait_entry;
    ATOMIC_STORE(&wb->signaled, false, ATOMIC_RELAXED);

    spinlock_lock(&thread->sched.wait_entry->lock);
    list_push_back(&thread->sched.wait_entry->wait_blocks_list, &wb->thread_node);
    spinlock_unlock(&thread->sched.wait_entry->lock);

    list_push_back(&timer->obj.wait_blocks, &wb->wait_object_node);

    spinlock_nodw_unlock(&timer->obj.lock);
    return false;
}

void sched_yield_internal(thread_state_t yield_state);

void sched_wait_multiple(wait_obj_t** objects, size_t count, wait_obj_wait_type_t type, uint64_t timeout_ms) {
    thread_t* thread = sched_arch_thread_current();
    wait_entry_t entry = {};

    thread->sched.wait_entry = &entry;
    entry.thread = thread;
    entry.lock = SPINLOCK_INIT;

    ATOMIC_STORE(&entry.type, type, ATOMIC_RELAXED);
    ATOMIC_STORE(&entry.waiting_on, type == WAIT_OBJ_WAIT_ALL ? count : 0, ATOMIC_RELAXED);
    ATOMIC_STORE(&entry.signaled, 0, ATOMIC_RELAXED);

    ATOMIC_STORE(&thread->sched.state, THREAD_STATE_WAITING_IN_PROGRESS, ATOMIC_RELEASE);

    timer_wait_t* timer = nullptr;
    if(timeout_ms != UINT64_MAX) {
        timer = timer_wait_create(timeout_ms);
        entry.timeout_obj = &timer->obj;
    }

    /// wait logic
    bool wait_aborted = false;

    if(type == WAIT_OBJ_WAIT_ANY) {
        wait_aborted = sched_wait_any(objects, count);
    } else if(type == WAIT_OBJ_WAIT_ALL) {
        wait_aborted = sched_wait_all(objects, count);
    }

    if(!wait_aborted && timer != nullptr) {
        wait_aborted = sched_wait_on_timer(timer);
    }

    if(wait_aborted) {
        /// handle abort path
        sched_wait_abort();
        ATOMIC_STORE(&thread->sched.state, THREAD_STATE_RUNNING, ATOMIC_RELEASE);
    } else {
        /// try commit
        thread_state_t expected = THREAD_STATE_WAITING_IN_PROGRESS;
        if(!ATOMIC_COMPARE_EXCHANGE_STRONG(&thread->sched.state, &expected, THREAD_STATE_WAITING, ATOMIC_ACQ_REL, ATOMIC_ACQUIRE)) {
            /// handle abort path
            sched_wait_abort();
            ATOMIC_STORE(&thread->sched.state, THREAD_STATE_RUNNING, ATOMIC_RELEASE);
        } else {
            sched_yield_internal(THREAD_STATE_WAITING);
            sched_wait_abort(); // clean up after wait
        }
    }

    if(timer) {
        timer_wait_free(timer);
    }
}
