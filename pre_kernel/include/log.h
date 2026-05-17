#pragma once
#include <stdarg.h>

// @todo: fix elysium tidy naming
// NOLINTBEGIN
void pk_log_vprint_raw(const char* fmt, va_list val);
void pk_log_print_raw(const char* fmt, ...);

void pk_log_vprint(const char* fmt, va_list val);
void pk_log_print(const char* fmt, ...);
// NOLINTEND
