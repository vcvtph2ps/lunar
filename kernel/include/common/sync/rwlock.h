#pragma once
#include <common/arch.h>
#include <common/interrupts/interrupt.h>
#include <lib/helpers.h>
#include <stdint.h>

typedef struct rwlock rwlock_t;

typedef struct {
    rwlock_t* inner;
} rwlock_read_t;

typedef struct {
    rwlock_t* inner;
} rwlock_write_t;

typedef struct rwlock {
    rwlock_read_t read_lock;
    rwlock_write_t write_lock;
    ATOMIC uint32_t value;
} rwlock_t;

void rwlock_init(rwlock_t* lock);

rwlock_write_t* rwlock_lock_write(rwlock_t* lock);
rwlock_read_t* rwlock_lock_read(rwlock_t* lock);

void rwlock_unlock_write(rwlock_write_t* lock);
void rwlock_unlock_read(rwlock_read_t* lock);
