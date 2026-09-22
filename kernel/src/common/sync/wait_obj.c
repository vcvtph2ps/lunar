#include <common/interrupts/interrupt.h>
#include <common/sched/sched.h>
#include <common/sched/thread.h>
#include <common/sync/spinlock.h>
#include <common/sync/wait_obj.h>
#include <lib/helpers.h>
#include <lib/list.h>

bool wait_obj_signal(wait_obj_t* obj, size_t count) {
    spinlock_nodw_lock(&obj->lock);
    list_node_t* node = obj->wait_blocks.head;

    // sync and notifcation ignore the count
    switch(obj->type) {
        case WAIT_OBJ_SYNCHRONIZATION: [[fallthrough]];
        case WAIT_OBJ_NOTIFICATION:    count = 1; break;
        case WAIT_OBJ_SEMAPHORE:       break;
    }

    ATOMIC_STORE(&obj->signal_state, count, ATOMIC_RELEASE);

    bool signaled_any = false;

    while(node != nullptr) {
        if(!wait_obj_is_signaled(obj)) {
            spinlock_nodw_unlock(&obj->lock);
            return signaled_any;
        }

        wait_block_t* wb = CONTAINER_OF(node, wait_block_t, wait_object_node);
        node = node->next;

        /// Claim the wait block so we don't end up signaling it twice
        if(ATOMIC_XCHG(&wb->signaled, true, ATOMIC_ACQ_REL)) {
            continue;
        }

        bool wake = false;
        if(ATOMIC_LOAD(&wb->entry->type, ATOMIC_RELAXED) == WAIT_OBJ_WAIT_ALL) {
            /// Signal the wait entry
            size_t done = ATOMIC_LOAD_ADD(&wb->entry->signaled, 1, ATOMIC_RELEASE) + 1;
            if(wb->obj == wb->entry->timeout_obj) {
                /// The timeout_obj instantly wakes the thread
                wake = true;
            } else {
                /// Else, check if all wait blocks are signaled
                wake = done >= ATOMIC_LOAD(&wb->entry->waiting_on, ATOMIC_ACQUIRE);
            }
        } else {
            wake = true;
        }

        /// Try abort the thread if it's in progress
        thread_state_t expected = THREAD_STATE_WAITING_IN_PROGRESS;
        if(ATOMIC_COMPARE_EXCHANGE_STRONG(&wb->entry->thread->sched.state, &expected, THREAD_STATE_WAITING_ABORTED, ATOMIC_ACQ_REL, ATOMIC_ACQUIRE)) {
            // The aborted thread will back itself out
            wait_obj_try_consume(obj);
            signaled_any = true;
            continue;
        }

        /// Check if the thread is aborted, if so it'll back itself out
        thread_state_t state = ATOMIC_LOAD(&wb->entry->thread->sched.state, ATOMIC_ACQUIRE);
        if(state == THREAD_STATE_WAITING_ABORTED) {
            continue;
        }

        /// Check if we can satisfy the wait
        if(state == THREAD_STATE_WAITING && wake) {
            sched_thread_schedule(wb->entry->thread);
            wait_obj_try_consume(obj);
            signaled_any = true;
            continue;
        }
    }

    spinlock_nodw_unlock(&obj->lock);
    return signaled_any;
}

bool wait_obj_is_signaled(wait_obj_t* obj) {
    bool state = ATOMIC_LOAD(&obj->signal_state, ATOMIC_ACQUIRE);
    return state > 0;
}

bool wait_obj_try_consume(wait_obj_t* obj) {
    bool success = false;

    switch(obj->type) {
        case WAIT_OBJ_NOTIFICATION:
            if(ATOMIC_LOAD(&obj->signal_state, ATOMIC_ACQUIRE) == 1) success = true;
            break;
        case WAIT_OBJ_SYNCHRONIZATION: {
            size_t count = ATOMIC_XCHG(&obj->signal_state, 0, ATOMIC_ACQUIRE);
            if(count == 1) {
                success = true;
            }
            break;
        }
        case WAIT_OBJ_SEMAPHORE: {
            // @todo: this could probably use some atomic black magic instead of a LOAD and LOAD_SUB, TOCOU bug???
            size_t count = ATOMIC_LOAD(&obj->signal_state, ATOMIC_ACQUIRE);
            if(count > 0) {
                ATOMIC_LOAD_SUB(&obj->signal_state, 1, ATOMIC_RELEASE);
                success = true;
            }
            break;
        }
    }

    return success;
}

void wait_obj_reset(wait_obj_t* obj) {
    ATOMIC_STORE(&obj->signal_state, 0, ATOMIC_RELEASE);
}
