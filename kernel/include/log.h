#pragma once
#include <stdarg.h>


int log_vprint_raw(const char* fmt, va_list val);
int log_print_raw(const char* fmt, ...);

int log_vprint(const char* fmt, va_list val);
int log_print(const char* fmt, ...);
