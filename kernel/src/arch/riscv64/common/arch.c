#include <arch/cpu_local.h>
#include <arch/hardware/sbi.h>
#include <common/arch.h>
#include <common/log.h>

#include "common/assert.h"

static void serial_sink(int c, void* ctx) {
    (void) ctx;
    arch_sbi_console_putchar((char) c);
}
void arch_log_init() {
    log_sink_t sink = {
        .min_level = LOG_LEVEL_STRC,
        .write = serial_sink,
        .ctx = nullptr,
    };

    if(!log_add_sink(&sink)) { LOG_OKAY("Failed to add log sink; serial output will not work :(\n"); }
}

uint64_t arch_get_core_id() {
    return CPU_LOCAL_READ(core_id);
}

void arch_spin_hint() {
    asm volatile("nop" ::: "memory");
}


[[noreturn]] uint64_t arch_read_timestamp_count() {
    arch_panic("arch_read_timestamp_count unimplemented");
}
