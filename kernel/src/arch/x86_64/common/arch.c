#include <arch/x86_64/hardware/lapic.h>
#include <arch/x86_64/io.h>
#include <common/arch.h>
#include <common/cpu_local.h>
#include <common/hardware/uart16550.h>
#include <common/interrupts/interrupt.h>
#include <common/log.h>
#include <lib/helpers.h>

[[gnu::always_inline]] void arch_spin_hint() {
    __asm__ volatile("pause" ::: "memory");
}

[[gnu::always_inline]] void arch_memory_fence(arch_memory_fence_type_t type) {
    switch(type) {
        case ARCH_MEMORY_FENCE_LOAD:   __asm__ volatile("lfence" ::: "memory"); return;
        case ARCH_MEMORY_FENCE_STORE:  __asm__ volatile("sfence" ::: "memory"); return;
        case ARCH_MEMORY_FENCE_MEMORY: __asm__ volatile("mfence" ::: "memory"); return;
        case ARCH_MEMORY_FENCE_FULL:   __asm__ volatile("mfence" ::: "memory"); return;
    }
}

[[gnu::always_inline]] void arch_wait_for_interrupt() {
    __asm__ volatile("hlt" ::: "memory");
}

[[noreturn]] void arch_shutdown_cpu() {
    (void) arch_interrupt_disable();
    arch_lapic_mask_all_interrupts();
    ATOMIC_STORE(&CPU_LOCAL_GET_SELF()->online, false, ATOMIC_RELEASE);

    while(1) {
        __asm__ volatile("hlt" ::: "memory");
    }
    __builtin_unreachable();
}

uint64_t arch_get_core_id() {
    return CPU_LOCAL_READ(core_id);
}

static void sink_debug(int c, void* ctx) {
    (void) ctx;
    if(!g_uart16550_works) {
        arch_io_port_write_u8(0xe9, (uint8_t) c);
    }
}

static void sink_debug_str(const char* s, size_t len, void* ctx) {
    (void) ctx;
    if(g_uart16550_works) {
        return;
    }
    for(size_t i = 0; i < len; i++) {
        arch_io_port_write_u8(0xe9, (uint8_t) s[i]);
    }
}

void arch_log_init() {
    log_sink_t debug_sink = {
        .min_level = LOG_LEVEL_STRC,
        .write = sink_debug,
        .write_str = sink_debug_str,
        .ctx = nullptr,
    };
    if(!log_add_sink(&debug_sink)) {
        // we really can't do much about that can we?
        while(1) {
            (void) arch_interrupt_disable();
            arch_wait_for_interrupt();
        }
    }

    arch_uart16550_early_setup();
}
