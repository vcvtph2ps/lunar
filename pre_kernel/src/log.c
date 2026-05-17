#include <nanoprintf/nanoprintf.h>

///
#include <arch/io.h>
#include <log.h>

static void debug_putc(int c, void* ctx) {
    (void) ctx;
    arch_io_port_write_u8(0xe9, (uint8_t) c);
}

void pk_log_vprint_raw(const char* fmt, va_list val) {
    npf_vpprintf(debug_putc, nullptr, fmt, val);
}

void pk_log_print_raw(const char* fmt, ...) {
    va_list val;
    va_start(val, fmt);
    npf_vpprintf(debug_putc, nullptr, fmt, val);
    va_end(val);
}


void pk_log_vprint(const char* fmt, va_list val) {
    npf_vpprintf(debug_putc, nullptr, "prekernel | ", nullptr);
    npf_vpprintf(debug_putc, nullptr, fmt, val);
}

void pk_log_print(const char* fmt, ...) {
    va_list val;
    va_start(val, fmt);
    npf_vpprintf(debug_putc, nullptr, "prekernel | ", nullptr);
    npf_vpprintf(debug_putc, nullptr, fmt, val);
    va_end(val);
}
