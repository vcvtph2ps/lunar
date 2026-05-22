#include <arch/interrupt.h>
#include <common/arch.h>
#include <common/spinlock.h>
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

[[nodiscard]] arch_interrupt_state_t spinlock_noint_lock(spinlock_no_int_t* lock) {
    arch_interrupt_state_t state = arch_interrupt_disable();
    spinlock_lock_raw(&lock->lock);
    return state;
}

void spinlock_noint_unlock(spinlock_no_int_t* lock, arch_interrupt_state_t interrupt_state) {
    spinlock_unlock_raw(&lock->lock);
    arch_interrupt_restore(interrupt_state);
}
