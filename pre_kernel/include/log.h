#pragma once
#include <stdarg.h>

// @todo: fix elysium tidy naming
// NOLINTBEGIN
int pk_log_vprint_raw(const char* fmt, va_list val);
int pk_log_print_raw(const char* fmt, ...);

int pk_log_vprint(const char* fmt, va_list val);
int pk_log_print(const char* fmt, ...);
// NOLINTEND
