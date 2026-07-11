#include <common/arch.h>
#include <common/interrupts/interrupt.h>
#include <common/sync/rwlock.h>
#include <lib/helpers.h>
#include <stdatomic.h>
#include <stdint.h>

void rwlock_init(rwlock_t* lock) {
    lock->read_lock.inner  = lock;
    lock->write_lock.inner = lock;
    ATOMIC_STORE(&lock->value, 0, ATOMIC_RELAXED);
}

rwlock_write_t* rwlock_lock_write(rwlock_t* lock) {
    uint32_t expected;

    for(;;) {
        expected = 0;

        if(ATOMIC_COMPARE_EXCHANGE_WEAK(&lock->value, &expected, UINT32_MAX, ATOMIC_ACQUIRE, ATOMIC_RELAXED)) { return &lock->write_lock; }

        arch_spin_hint();
    }

    return &lock->write_lock;
}

rwlock_read_t* rwlock_lock_read(rwlock_t* lock) {
    uint32_t old = ATOMIC_LOAD(&lock->value, ATOMIC_RELAXED);

    for(;;) {
        if(old == UINT32_MAX) {
            old = ATOMIC_LOAD(&lock->value, ATOMIC_RELAXED);
            arch_spin_hint();
            continue;
        }


        if(ATOMIC_COMPARE_EXCHANGE_WEAK(&lock->value, &old, old + 1, ATOMIC_ACQUIRE, ATOMIC_RELAXED)) { return &lock->read_lock; }
    }
}

void rwlock_unlock_write(rwlock_write_t* lock) {
    ATOMIC_STORE(&lock->inner->value, 0, ATOMIC_RELEASE);
}

void rwlock_unlock_read(rwlock_read_t* lock) {
    ATOMIC_LOAD_SUB(&lock->inner->value, 1, ATOMIC_RELEASE);
}
