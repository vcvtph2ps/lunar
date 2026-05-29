#pragma once
#include <common/arch.h>

// NOLINTBEGIN
#define assert(expr)                                                                           \
    do {                                                                                       \
        if(EXPECT_UNLIKELY(!(expr))) {                                                         \
            arch_panic("Assertion failed: %s, file %s, line %d\n", #expr, __FILE__, __LINE__); \
        }                                                                                      \
    } while(0)

#define assertf(expr, fmt, ...)                                                                                        \
    do {                                                                                                               \
        if(EXPECT_UNLIKELY(!(expr))) {                                                                                 \
            arch_panic("Assertion failed: %s, file %s, line %d: " fmt "\n", #expr, __FILE__, __LINE__, ##__VA_ARGS__); \
        }                                                                                                              \
    } while(0)

#define EXPECT_LIKELY(V) (__builtin_expect(!!(V), 1))
#define EXPECT_UNLIKELY(V) (__builtin_expect(!!(V), 0))

#define ASSERT_UNREACHABLE()                                               \
    do {                                                                   \
        arch_panic("Unreachable, file %s, line %d\n", __FILE__, __LINE__); \
    } while(0)
// NOLINTEND
