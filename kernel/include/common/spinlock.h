#pragma once
#include <arch/interrupt.h>
#include <lib/helpers.h>
#include <stdint.h>

/**
 * @brief A spinlock that also disables interrupts on acquire and restores them on release.
 * @note These should only be used when the lock could be grabbed from a hardirq context
 */
typedef struct {
    ATOMIC uint32_t lock;
} spinlock_no_int_t;

#define SPINLOCK_NO_INT_INIT ((spinlock_no_int_t) { 0 })

/**
 * @brief Acquires a spinlock, hard disables interrupts
 * @param lock Pointer to the spinlock to acquire.
 */
[[nodiscard]] arch_interrupt_state_t spinlock_noint_lock(spinlock_no_int_t* lock);

/**
 * @brief Releases a spinlock and restores interrupts
 * @param lock Pointer to the spinlock to release.
 */
void spinlock_noint_unlock(spinlock_no_int_t* lock, arch_interrupt_state_t interrupt_state);
