#include <arch/hardware/16550uart.h>
#include <arch/interrupt.h>
#include <common/arch.h>
#include <common/cpu_local.h>
#include <common/log.h>

void arch_spin_hint() {
    __asm__ volatile("pause" ::: "memory");
}

void arch_wait_for_interrupt() {
    __asm__ volatile("hlt" ::: "memory");
}

uint64_t arch_get_core_id() {
    return CPU_LOCAL_READ(core_id);
}

static void sink_debug(int c, void* ctx) {
    (void) ctx;
    arch_io_port_write_u8(0xe9, (uint8_t) c);
}

void arch_log_init() {
    log_sink_t debug_sink = {
        .min_level = LOG_LEVEL_STRC,
        .write = sink_debug,
        .ctx = nullptr,
    };
    if(!log_add_sink(&debug_sink)) {
        // we really can't do much about that can we?
        while(1) {
            (void) arch_interrupt_disable();
            arch_wait_for_interrupt();
        }
    }

    arch_16550uart_early_setup();
}
