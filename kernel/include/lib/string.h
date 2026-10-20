#pragma once
#include <stddef.h>

// NOLINTBEGIN
void* memory_set(void* dest, int ch, size_t count);
void* memory_zero(void* dest, size_t count);
void* memory_copy(void* restrict dest, const void* restrict src, size_t count);
void memory_move(void* dest, const void* src, size_t count);
[[nodiscard]] int memory_compare(const void* lhs, const void* rhs, size_t count);

[[nodiscard]] int string_find_first_instance(const char* s, char c, size_t size);
[[nodiscard]] int string_compare(const char* s1, const char* s2);
[[nodiscard]] int string_compare_case(const char* s1, const char* s2);
[[nodiscard]] int string_compare_n_case(const char* s1, const char* s2, size_t n);
[[nodiscard]] int string_length(const char* str);
[[nodiscard]] size_t string_length_n(const char* str, size_t maxlen);
[[nodiscard]] char* string_find_last_instance(const char* str, int c);
[[nodiscard]] void* memory_find_byte(const void* s, int c, size_t count);
// NOLINTEND
