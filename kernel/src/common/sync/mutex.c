#include <arch/interrupts/interrupt.h>
#include <common/arch.h>
#include <common/assert.h>
#include <common/sched/sched.h>
#include <common/sched/thread.h>
#include <common/spinlock.h>
#include <common/sync/mutex.h>
#include <lib/helpers.h>

static bool try_lock(mutex_t* mutex, bool weak) {
    mutex_state_t state = MUTEX_STATE_UNLOCKED;
    return __atomic_compare_exchange_n(&mutex->state, &state, MUTEX_STATE_LOCKED, weak, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
}

void mutex_acquire(mutex_t* mutex) {
    if(EXPECT_LIKELY(try_lock(mutex, true))) return;

    for(int i = 0; i < 50; i++) {
        if(EXPECT_LIKELY(try_lock(mutex, true))) return;
        sched_yield(THREAD_STATE_READY);
    }

    arch_interrupt_state_t previous_state = spinlock_noint_lock(&mutex->lock);
    if(EXPECT_LIKELY(__atomic_exchange_n(&mutex->state, MUTEX_STATE_CONTESTED, __ATOMIC_ACQ_REL) != MUTEX_STATE_UNLOCKED)) {
        spinlock_noint_unlock(&mutex->lock, previous_state);
        wait_queue_join(&mutex->wait_queue);
        previous_state = spinlock_noint_lock(&mutex->lock);
    } else {
        __atomic_store_n(&mutex->state, MUTEX_STATE_LOCKED, __ATOMIC_RELEASE);
    }

    spinlock_noint_unlock(&mutex->lock, previous_state);
}

void mutex_release(mutex_t* mutex) {
    mutex_state_t state = MUTEX_STATE_LOCKED;
    if(EXPECT_LIKELY(__atomic_compare_exchange_n(&mutex->state, &state, MUTEX_STATE_UNLOCKED, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))) return;

    arch_interrupt_state_t previous_state = spinlock_noint_lock(&mutex->lock);

    assert(state == MUTEX_STATE_CONTESTED);

    // if the mutex is contested but there are no threads in the wait queue
    while(wait_queue_empty(&mutex->wait_queue)) {
        spinlock_noint_unlock(&mutex->lock, previous_state);
        sched_yield(THREAD_STATE_READY);
        previous_state = spinlock_noint_lock(&mutex->lock);
    }

    assert(mutex->wait_queue.list.count != 0);

    thread_t* thread = wait_queue_pop(&mutex->wait_queue);
    sched_thread_schedule(thread);

    if(mutex->wait_queue.list.count == 0) __atomic_store_n(&mutex->state, MUTEX_STATE_LOCKED, __ATOMIC_RELEASE);

    spinlock_noint_unlock(&mutex->lock, previous_state);
}
