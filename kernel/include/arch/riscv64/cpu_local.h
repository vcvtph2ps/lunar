#pragma once
#include <common/interrupts/ipi.h>
#include <common/sched/sched.h>
#include <lib/list.h>
#include <memory/pmm.h>
#include <stddef.h>
#include <stdint.h>

typedef struct arch_cpu_local arch_cpu_local_t;

struct [[gnu::aligned(64)]] arch_cpu_local {
    arch_cpu_local_t* self;
    uint32_t tsc_ticks_per_us; // only here so the damn thing compiles
    uint32_t core_id;
    uint32_t hart_id;

    struct {
        uint32_t counter;
        bool yield_pending;
        list_t queue;
    } defered_work;

    pmm_t pmm;

    scheduler_t scheduler;
    ipi_request_t* ipi_queue;
};

uint32_t arch_cpu_local_get_core_hart_id(uint32_t core_id);

static inline arch_cpu_local_t* arch_cpu_local_get_self(void) {
    arch_cpu_local_t* self;
    asm volatile("csrr %0, sscratch" : "=r"(self));
    return self;
}

// NOLINTBEGIN
#define CPU_LOCAL_READ(field) (__atomic_load_n(&arch_cpu_local_get_self()->field, __ATOMIC_SEQ_CST))

#define CPU_LOCAL_WRITE(field, value)                                                                                                                              \
    ({                                                                                                                                                             \
        static_assert(__builtin_types_compatible_p(typeof(((arch_cpu_local_t*) nullptr)->field), typeof(value)), "member type and value type are not compatible"); \
        typeof(((arch_cpu_local_t*) nullptr)->field) __v = (value);                                                                                                \
        __atomic_store_n(&arch_cpu_local_get_self()->field, __v, __ATOMIC_SEQ_CST);                                                                                \
    })

#define CPU_LOCAL_EXCHANGE(FIELD, VALUE)                                                                                                                           \
    ({                                                                                                                                                             \
        static_assert(__builtin_types_compatible_p(typeof(((arch_cpu_local_t*) nullptr)->FIELD), typeof(VALUE)), "member type and value type are not compatible"); \
        typeof(((arch_cpu_local_t*) nullptr)->FIELD) __v = (VALUE);                                                                                                \
        __atomic_exchange_n(&arch_cpu_local_get_self()->FIELD, __v, __ATOMIC_SEQ_CST);                                                                             \
    })

#define CPU_LOCAL_INC(FIELD) ({ __atomic_fetch_add(&arch_cpu_local_get_self()->FIELD, 1, __ATOMIC_SEQ_CST); })
#define CPU_LOCAL_DEC(FIELD) ({ __atomic_fetch_sub(&arch_cpu_local_get_self()->FIELD, 1, __ATOMIC_SEQ_CST); })

#define CPU_LOCAL_GET_SELF() CPU_LOCAL_READ(self)
#define CPU_LOCAL_GET_CURRENT_THREAD() CPU_LOCAL_READ(current_thread)
// NOLINTEND
