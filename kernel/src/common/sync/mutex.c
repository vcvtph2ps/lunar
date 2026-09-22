#include <common/arch.h>
#include <common/assert.h>
#include <common/interrupts/interrupt.h>
#include <common/sched/sched.h>
#include <common/sched/thread.h>
#include <common/sync/mutex.h>
#include <common/sync/spinlock.h>
#include <common/sync/wait_obj.h>
#include <lib/helpers.h>
#include <lib/types.h>

static bool try_lock(mutex_t* mutex) {
    mutex_state_t state = MUTEX_STATE_UNLOCKED;
    return ATOMIC_COMPARE_EXCHANGE_STRONG(&mutex->state, &state, MUTEX_STATE_LOCKED, ATOMIC_ACQ_REL, ATOMIC_RELAXED);
}

void mutex_acquire(mutex_t* mutex) {
    sched_preempt_disable();
    if(EXPECT_LIKELY(try_lock(mutex))) {
        sched_preempt_enable();
        return;
    }

    for(int i = 0; i < 50; i++) {
        if(EXPECT_LIKELY(try_lock(mutex))) {
            sched_preempt_enable();
            return;
        }
    }

    arch_interrupt_state_t previous_state = spinlock_noint_lock(&mutex->lock);
    if(EXPECT_LIKELY(ATOMIC_XCHG(&mutex->state, MUTEX_STATE_CONTESTED, ATOMIC_ACQ_REL) != MUTEX_STATE_UNLOCKED)) {
        spinlock_noint_unlock(&mutex->lock, previous_state);
        sched_preempt_enable();

        ATOMIC_LOAD_ADD(&mutex->waiters, 1, ATOMIC_RELEASE);
        sched_wait_single(&mutex->wait_obj, UINT64_MAX);

        previous_state = spinlock_noint_lock(&mutex->lock);
        sched_preempt_disable();
    } else {
        ATOMIC_STORE(&mutex->state, MUTEX_STATE_LOCKED, ATOMIC_RELEASE);
    }

    spinlock_noint_unlock(&mutex->lock, previous_state);
    sched_preempt_enable();
}

void mutex_release(mutex_t* mutex) {
    sched_preempt_disable();
    mutex_state_t state = MUTEX_STATE_LOCKED;
    if(EXPECT_LIKELY(ATOMIC_COMPARE_EXCHANGE_STRONG(&mutex->state, &state, MUTEX_STATE_UNLOCKED, ATOMIC_ACQ_REL, ATOMIC_RELAXED))) {
        sched_preempt_enable();
        return;
    }

    arch_interrupt_state_t previous_state = spinlock_noint_lock(&mutex->lock);
    assert(state == MUTEX_STATE_CONTESTED);

    if(ATOMIC_LOAD_SUB(&mutex->waiters, 1, ATOMIC_RELEASE) == 1) {
        ATOMIC_STORE(&mutex->state, MUTEX_STATE_LOCKED, ATOMIC_RELEASE);
    }

    wait_obj_signal(&mutex->wait_obj, 1);

    spinlock_noint_unlock(&mutex->lock, previous_state);
    sched_preempt_enable();
}
