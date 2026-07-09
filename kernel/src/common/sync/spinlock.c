#include <common/arch.h>
#include <common/interrupts/dw.h>
#include <common/interrupts/interrupt.h>
#include <common/sched/sched.h>
#include <common/sync/spinlock.h>
#include <lib/helpers.h>
#include <stdint.h>

[[nodiscard]] static inline bool spinlock_try_lock(ATOMIC_PARAM uint32_t* lock) {
    return !ATOMIC_XCHG(lock, 1, ATOMIC_ACQUIRE);
}

static inline void spinlock_unlock_raw(ATOMIC_PARAM uint32_t* lock) {
    ATOMIC_STORE(lock, 0, ATOMIC_RELEASE);
}

static inline void spinlock_lock_raw(ATOMIC_PARAM uint32_t* lock) {
    while(true) {
        if(spinlock_try_lock(lock)) return;
        while(ATOMIC_LOAD(lock, ATOMIC_RELAXED)) { arch_spin_hint(); }
    }
}

void spinlock_lock(spinlock_t* lock) {
    sched_preempt_disable();
    spinlock_lock_raw(&lock->lock);
}

void spinlock_unlock(spinlock_t* lock) {
    spinlock_unlock_raw(&lock->lock);
    sched_preempt_enable();
}

void spinlock_nodw_lock(spinlock_no_dw_t* lock) {
    sched_preempt_disable();
    dw_status_disable();
    spinlock_lock_raw(&lock->lock);
}

void spinlock_nodw_unlock(spinlock_no_dw_t* lock) {
    spinlock_unlock_raw(&lock->lock);
    dw_status_enable();
    sched_preempt_enable();
}


// @todo: do we need to disable dw here too? maybe not since hardirqs should already be disabled when this is used, but maybe we should just to be safe?
[[nodiscard]] arch_interrupt_state_t spinlock_noint_lock(spinlock_no_int_t* lock) {
    arch_interrupt_state_t state = arch_interrupt_disable();
    spinlock_lock_raw(&lock->lock);
    return state;
}

void spinlock_noint_unlock(spinlock_no_int_t* lock, arch_interrupt_state_t interrupt_state) {
    spinlock_unlock_raw(&lock->lock);
    arch_interrupt_restore(interrupt_state);
}
