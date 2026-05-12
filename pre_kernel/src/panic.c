#include <log.h>
#include <stdarg.h>

[[noreturn]] void pk_panic(const char* format, ...) {
    va_list args;
    va_start(args, message);
    pk_log_print("prekernel panic: ");
    pk_log_vprint(format, args);
    pk_log_print("\n");
    va_end(args);
    while(1);
}
