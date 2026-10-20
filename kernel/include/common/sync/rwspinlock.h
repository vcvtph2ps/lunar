#pragma once
#include <common/arch.h>
#include <common/interrupts/interrupt.h>
#include <lib/helpers.h>
#include <stdint.h>

typedef struct rwspinlock rwspinlock_t;

typedef struct {
    rwspinlock_t* inner;
} rwspinlock_read_t;

typedef struct {
    rwspinlock_t* inner;
} rwspinlock_write_t;

typedef struct rwspinlock {
    rwspinlock_read_t read_lock;
    rwspinlock_write_t write_lock;
    ATOMIC uint32_t value;
} rwspinlock_t;

void rwspinlock_init(rwspinlock_t* lock);

rwspinlock_write_t* rwspinlock_lock_write(rwspinlock_t* lock);
rwspinlock_read_t* rwspinlock_lock_read(rwspinlock_t* lock);

void rwspinlock_unlock_write(rwspinlock_write_t* lock);
void rwspinlock_unlock_read(rwspinlock_read_t* lock);
