#pragma once
#include <stddef.h>

// NOLINTBEGIN
void memset(void* dest, int ch, size_t count);
void memcpy(void* dest, const void* src, size_t count);
void memmove(void* dest, const void* src, size_t count);
[[nodiscard]] int memcmp(const void* lhs, const void* rhs, size_t count);

[[nodiscard]] int strcmp(const char* s1, const char* s2);
[[nodiscard]] int strlen(const char* str);
// NOLINTEND
