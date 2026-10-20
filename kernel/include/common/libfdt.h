#pragma once
// NOLINTBEGIN
#define LIBFDT_ENV_H

#include <lib/string.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define U32_MAX ((uint32_t) ~0U)
#define S32_MAX ((int32_t) (U32_MAX >> 1))

#define INT_MAX S32_MAX
#define UINT_MAX U32_MAX

typedef uint16_t fdt16_t;
typedef uint32_t fdt32_t;
typedef uint64_t fdt64_t;

#define bswap_16(x) __builtin_bswap16(x)
#define bswap_32(x) __builtin_bswap32(x)
#define bswap_64(x) __builtin_bswap64(x)

#define fdt16_to_cpu(x) bswap_16(x)
#define cpu_to_fdt16(x) bswap_16(x)
#define fdt32_to_cpu(x) bswap_32(x)
#define cpu_to_fdt32(x) bswap_32(x)
#define fdt64_to_cpu(x) bswap_64(x)
#define cpu_to_fdt64(x) bswap_64(x)

// standard names provided as aliases by lib/string.c, required by libfdt
void* memset(void* dest, int ch, size_t count);
void* memcpy(void* restrict dest, const void* restrict src, size_t count);
void memmove(void* dest, const void* src, size_t count);
[[nodiscard]] int memcmp(const void* lhs, const void* rhs, size_t count);
[[nodiscard]] void* memchr(const void* s, int c, size_t count);
[[nodiscard]] size_t strlen(const char* str);
[[nodiscard]] size_t strnlen(const char* str, size_t maxlen);
[[nodiscard]] char* strrchr(const char* str, int c);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdocumentation"
#include <libfdt.h>
#pragma clang diagnostic pop
// NOLINTEND
