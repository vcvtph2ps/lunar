#pragma once
#include <common/arch.h>
#include <common/interrupts/interrupt.h>
#include <lib/helpers.h>
#include <stdint.h>

/**
 * @brief A spinlock that disables preemption on acquire and restores it on release
 * @note These should be used when the lock cannot be grabbed from deferred work or hardirqs
 */
typedef struct {
    ATOMIC uint32_t lock;
} spinlock_t;

/**
 * @brief A spinlock that also disables deferred on acquire and restores it on release
 * @note These should only be used when the lock could be grabbed from a deferred work context but **not** from a hardirq context
 */
typedef struct {
    ATOMIC uint32_t lock;
} spinlock_no_dpc_t;

/**
 * @brief A spinlock that also disables interrupts on acquire and restores them on release
 * @note These should only be used when the lock could be grabbed from a hardirq context
 */
typedef struct {
    ATOMIC uint32_t lock;
} spinlock_no_int_t;

#define SPINLOCK_INIT ((spinlock_t) { 0 })
#define SPINLOCK_NO_DPC_INIT ((spinlock_no_dpc_t) { 0 })
#define SPINLOCK_NO_INT_INIT ((spinlock_no_int_t) { 0 })

/**
 * @brief Acquires a spinlock and increments the preemption counter
 */
void spinlock_lock(spinlock_t* lock);

/**
 * @brief Releases a spinlock and decrements the preemption counter
 */
void spinlock_unlock(spinlock_t* lock);

/**
 * @brief Acquires a spinlock, increments the preemption and deferred work
 * counters
 */
void spinlock_no_dpc_lock(spinlock_no_dpc_t* lock);

/**
 * @brief Releases a spinlock and decrements the preemption and deferred work
 * counters
 */
void spinlock_no_dpc_unlock(spinlock_no_dpc_t* lock);

/**
 * @brief Acquires a spinlock, hard disables interrupts
 */
[[nodiscard]] arch_interrupt_state_t spinlock_noint_lock(spinlock_no_int_t* lock);

/**
 * @brief Releases a spinlock and restores interrupts
 */
void spinlock_noint_unlock(spinlock_no_int_t* lock, arch_interrupt_state_t interrupt_state);
