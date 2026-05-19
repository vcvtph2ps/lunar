#pragma once

// NOLINTBEGIN
#define ATOMIC [[clang::annotate("atomic_only")]]
#define ATOMIC_PARAM [[clang::annotate("atomic_only_param")]]

#define ATOMIC_ACQUIRE __ATOMIC_ACQUIRE
#define ATOMIC_RELEASE __ATOMIC_RELEASE
#define ATOMIC_RELAXED __ATOMIC_RELAXED

#define ATOMIC_XCHG(OBJ, VAL, ORDER) __atomic_exchange_n((OBJ), (VAL), ORDER)
#define ATOMIC_LOAD(OBJ, ORDER) __atomic_load_n((OBJ), ORDER)
#define ATOMIC_STORE(OBJ, VAL, ORDER) __atomic_store_n((OBJ), (VAL), ORDER)
#define ATOMIC_LOAD_ADD(OBJ, VAL, ORDER) __atomic_fetch_add((OBJ), (VAL), ORDER)
#define ATOMIC_LOAD_SUB(OBJ, VAL, ORDER) __atomic_fetch_sub((OBJ), (VAL), ORDER)

/**
 * @brief Get the container of a child struct.
 * @param PTR Pointer to the child struct
 * @param TYPE Type of the container
 * @param MEMBER Name of the child member in the container
 */
#define CONTAINER_OF(PTR, TYPE, MEMBER)                                                                                               \
    ({                                                                                                                                \
        static_assert(__builtin_types_compatible_p(typeof(((TYPE*) 0)->MEMBER), typeof(*PTR)), "member type does not match pointer"); \
        (TYPE*) (((uintptr_t) (PTR)) - __builtin_offsetof(TYPE, MEMBER));                                                             \
    })
// NOLINTEND
