#include <common/arch.h>
#include <common/assert.h>
#include <common/interrupts/interrupt.h>
#include <common/sched/sched.h>
#include <common/sched/thread.h>
#include <common/sync/lock.h>
#include <common/sync/spinlock.h>
#include <common/sync/wait_obj.h>
#include <lib/helpers.h>
#include <lib/types.h>

static bool try_lock(lock_t* lock) {
    lock_state_t state = LOCK_STATE_UNLOCKED;
    return ATOMIC_COMPARE_EXCHANGE_STRONG(&lock->state, &state, LOCK_STATE_LOCKED, ATOMIC_ACQ_REL, ATOMIC_RELAXED);
}

void lock_acquire(lock_t* lock) {
    sched_preempt_disable();
    if(EXPECT_LIKELY(try_lock(lock))) {
        sched_preempt_enable();
        return;
    }

    for(int i = 0; i < 50; i++) {
        if(EXPECT_LIKELY(try_lock(lock))) {
            sched_preempt_enable();
            return;
        }
    }

    arch_interrupt_state_t previous_state = spinlock_noint_lock(&lock->lock);
    if(EXPECT_LIKELY(ATOMIC_XCHG(&lock->state, LOCK_STATE_CONTESTED, ATOMIC_ACQ_REL) != LOCK_STATE_UNLOCKED)) {
        spinlock_noint_unlock(&lock->lock, previous_state);
        sched_preempt_enable();

        ATOMIC_LOAD_ADD(&lock->waiters, 1, ATOMIC_RELEASE);
        sched_wait_single(&lock->wait_obj, UINT64_MAX);

        previous_state = spinlock_noint_lock(&lock->lock);
        sched_preempt_disable();
    } else {
        ATOMIC_STORE(&lock->state, LOCK_STATE_LOCKED, ATOMIC_RELEASE);
    }

    spinlock_noint_unlock(&lock->lock, previous_state);
    sched_preempt_enable();
}

void lock_release(lock_t* lock) {
    sched_preempt_disable();
    lock_state_t state = LOCK_STATE_LOCKED;
    if(EXPECT_LIKELY(ATOMIC_COMPARE_EXCHANGE_STRONG(&lock->state, &state, LOCK_STATE_UNLOCKED, ATOMIC_ACQ_REL, ATOMIC_RELAXED))) {
        sched_preempt_enable();
        return;
    }

    arch_interrupt_state_t previous_state = spinlock_noint_lock(&lock->lock);
    assert(state == LOCK_STATE_CONTESTED);

    if(ATOMIC_LOAD_SUB(&lock->waiters, 1, ATOMIC_RELEASE) == 1) {
        ATOMIC_STORE(&lock->state, LOCK_STATE_LOCKED, ATOMIC_RELEASE);
    }

    wait_obj_signal(&lock->wait_obj, 1);

    spinlock_noint_unlock(&lock->lock, previous_state);
    sched_preempt_enable();
}
