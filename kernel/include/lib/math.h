#pragma once

#define MATH_DIV_CEIL(DIVIDEND, DIVISOR) (((DIVIDEND) + (DIVISOR) - 1) / (DIVISOR))
#define MATH_CEIL(VALUE, PRECISION) (MATH_DIV_CEIL((VALUE), (PRECISION)) * (PRECISION))
#define MATH_FLOOR(VALUE, PRECISION) (((VALUE) / (PRECISION)) * (PRECISION))

#define MATH_IS_POWER_OF_TWO(x) ((x) != 0 && ((x) & ((x) - 1)) == 0)
#define MATH_ALIGN_UP(x, align) ((((uintptr_t) (x)) + ((align) - 1)) & ~((uintptr_t) ((align) - 1)))
#define MATH_ALIGN_DOWN(x, align) (((uintptr_t) (x)) & ~((uintptr_t) ((align) - 1)))

#define MATH_MIN(a, b) ((a) < (b) ? (a) : (b))
#define MATH_MAX(a, b) ((a) > (b) ? (a) : (b))

[[nodiscard]] static inline int math_min(int a, int b) {
    return a < b ? a : b;
}

[[nodiscard]] static inline int math_max(int a, int b) {
    return a > b ? a : b;
}
