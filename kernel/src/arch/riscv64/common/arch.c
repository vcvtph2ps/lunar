#include <arch/riscv64/hardware/sbi.h>
#include <common/arch.h>
#include <common/assert.h>
#include <common/cpu_local.h>
#include <common/log.h>

[[gnu::always_inline]] void arch_spin_hint() {
    asm volatile("pause" ::: "memory");
}

[[gnu::always_inline]] void arch_memory_fence(arch_memory_fence_type_t type) {
    switch(type) {
        case ARCH_MEMORY_FENCE_LOAD:   __asm__ volatile("fence r, r" ::: "memory"); return;
        case ARCH_MEMORY_FENCE_STORE:  __asm__ volatile("fence w, w" ::: "memory"); return;
        case ARCH_MEMORY_FENCE_MEMORY: __asm__ volatile("fence rw, rw" ::: "memory"); return;
        case ARCH_MEMORY_FENCE_FULL:   __asm__ volatile("fence iorw, iorw" ::: "memory"); return;
    }
}


[[gnu::always_inline]] void arch_wait_for_interrupt() {
    __asm__ volatile("wfi" ::: "memory");
}

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

    if(!log_add_sink(&sink)) {
        LOG_OKAY("Failed to add log sink; serial output will not work :(\n");
    }
}

uint64_t arch_get_core_id() {
    return CPU_LOCAL_READ(core_id);
}
