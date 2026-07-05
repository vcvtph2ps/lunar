#pragma once
#include <arch/internal/gdt.h>
#include <common/sched/sched.h>
#include <lib/list.h>
#include <stddef.h>
#include <stdint.h>

typedef struct arch_cpu_local arch_cpu_local_t;

struct [[gnu::aligned(64)]] arch_cpu_local {
    arch_cpu_local_t* self;

    uint32_t core_id;
    uint32_t lapic_id;

    struct {
        uint32_t counter;
        bool yield_pending;
        list_t queue;
    } defered_work;

    uint32_t tsc_ticks_per_us;
    scheduler_t scheduler;
    arch_gdt_t gdt;
    arch_gdt_tss_t tss;
};

uint32_t arch_cpu_local_get_core_lapic_id(uint32_t core_id);

// NOLINTBEGIN
#define CPU_LOCAL_READ(field)                                                                    \
    ({                                                                                           \
        typeof(((arch_cpu_local_t*) nullptr)->field) value;                                      \
        asm volatile("mov %%gs:%c1, %0" : "=r"(value) : "i"(offsetof(arch_cpu_local_t, field))); \
        value;                                                                                   \
    })

#define CPU_LOCAL_WRITE(field, value)                                                                                                                              \
    ({                                                                                                                                                             \
        static_assert(__builtin_types_compatible_p(typeof(((arch_cpu_local_t*) nullptr)->field), typeof(value)), "member type and value type are not compatible"); \
        typeof(((arch_cpu_local_t*) nullptr)->field) v = (value);                                                                                                  \
        asm volatile("mov %0, %%gs:%c1" : : "r"(v), "i"(offsetof(arch_cpu_local_t, field)));                                                                       \
    })

#define CPU_LOCAL_EXCHANGE(FIELD, VALUE)                                                                                                                           \
    ({                                                                                                                                                             \
        static_assert(__builtin_types_compatible_p(typeof(((arch_cpu_local_t*) nullptr)->FIELD), typeof(VALUE)), "member type and value type are not compatible"); \
        typeof(((arch_cpu_local_t*) nullptr)->FIELD) value = (VALUE);                                                                                              \
        asm volatile("xchg %0, %%gs:%c1" : "+r"(value) : "i"(offsetof(arch_cpu_local_t, FIELD)) : "memory");                                                       \
        value;                                                                                                                                                     \
    })

#define CPU_LOCAL_INC_64(FIELD) ({ asm volatile("incq %%gs:%c0" : : "i"(offsetof(arch_cpu_local_t, FIELD)) : "memory"); })
#define CPU_LOCAL_INC_32(FIELD) ({ asm volatile("incl %%gs:%c0" : : "i"(offsetof(arch_cpu_local_t, FIELD)) : "memory"); })
#define CPU_LOCAL_INC_16(FIELD) ({ asm volatile("incw %%gs:%c0" : : "i"(offsetof(arch_cpu_local_t, FIELD)) : "memory"); })
#define CPU_LOCAL_INC_8(FIELD) ({ asm volatile("incb %%gs:%c0" : : "i"(offsetof(arch_cpu_local_t, FIELD)) : "memory"); })

#define CPU_LOCAL_INC(FIELD)                    \
    _Generic(                                   \
        (((arch_cpu_local_t*) nullptr)->FIELD), \
        uint64_t: CPU_LOCAL_INC_64(FIELD),      \
        int64_t: CPU_LOCAL_INC_64(FIELD),       \
        uint32_t: CPU_LOCAL_INC_32(FIELD),      \
        int32_t: CPU_LOCAL_INC_32(FIELD),       \
        uint16_t: CPU_LOCAL_INC_16(FIELD),      \
        int16_t: CPU_LOCAL_INC_16(FIELD),       \
        uint8_t: CPU_LOCAL_INC_8(FIELD),        \
        int8_t: CPU_LOCAL_INC_8(FIELD)          \
    )

#define CPU_LOCAL_DEC_64(FIELD) ({ asm volatile("decq %%gs:%c0" : : "i"(offsetof(arch_cpu_local_t, FIELD)) : "memory"); })
#define CPU_LOCAL_DEC_32(FIELD) ({ asm volatile("decl %%gs:%c0" : : "i"(offsetof(arch_cpu_local_t, FIELD)) : "memory"); })
#define CPU_LOCAL_DEC_16(FIELD) ({ asm volatile("decw %%gs:%c0" : : "i"(offsetof(arch_cpu_local_t, FIELD)) : "memory"); })
#define CPU_LOCAL_DEC_8(FIELD) ({ asm volatile("decb %%gs:%c0" : : "i"(offsetof(arch_cpu_local_t, FIELD)) : "memory"); })

#define CPU_LOCAL_DEC(FIELD)                    \
    _Generic(                                   \
        (((arch_cpu_local_t*) nullptr)->FIELD), \
        uint64_t: CPU_LOCAL_DEC_64(FIELD),      \
        int64_t: CPU_LOCAL_DEC_64(FIELD),       \
        uint32_t: CPU_LOCAL_DEC_32(FIELD),      \
        int32_t: CPU_LOCAL_DEC_32(FIELD),       \
        uint16_t: CPU_LOCAL_DEC_16(FIELD),      \
        int16_t: CPU_LOCAL_DEC_16(FIELD),       \
        uint8_t: CPU_LOCAL_DEC_8(FIELD),        \
        int8_t: CPU_LOCAL_DEC_8(FIELD)          \
    )


#define CPU_LOCAL_GET_SELF() CPU_LOCAL_READ(self)
#define CPU_LOCAL_GET_CURRENT_THREAD() CPU_LOCAL_READ(current_thread)
// NOLINTEND
