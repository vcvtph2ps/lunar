#include <arch/riscv64/cpu_local.h>
#include <arch/riscv64/interrupts/interrupt.h>
#include <common/arch.h>
#include <common/interrupts/ipi.h>
#include <common/log.h>
#include <lib/helpers.h>
#include <lib/ksym.h>
#include <memory/vm.h>
#include <stdarg.h>

static const char* g_name_table[20] = { "Instruction address misaligned",
                                        "Instruction access fault",
                                        "Illegal instruction",
                                        "Breakpoint",
                                        "Load address misaligned",
                                        "Load access fault",
                                        "Store/AMO address misaligned",
                                        "Store/AMO access fault",
                                        "Environment call from U-mode",
                                        "Environment call from S-mode",
                                        "Reserved",
                                        "Reserved",
                                        "Instruction page fault",
                                        "Load page fault",
                                        "Reserved",
                                        "Store/AMO page fault",
                                        "Reserved",
                                        "Reserved",
                                        "Software check",
                                        "Hardware error" };

ATOMIC uint32_t g_panicking_core = 0xffffffff;

[[clang::always_inline]] static void panic_begin() {
    (void) arch_interrupt_disable();
    arch_memory_fence(ARCH_MEMORY_FENCE_FULL);

    uint32_t core_id = CPU_LOCAL_READ(core_id);
    uint32_t old_value = ATOMIC_XCHG(&g_panicking_core, core_id, ATOMIC_SEQ_CST);

    if(old_value == core_id) {
        log_print_lockless(LOG_LEVEL_FAIL, "Kernel panic on core: %d (recursive panic)\n", core_id);
        arch_shutdown_cpu();
    }

    if(old_value != 0xffffffff) {
        arch_shutdown_cpu();
    }

    // @note: We set these to false, we need to be able to grab any kind of lock. and it doesn't matter anyway the kernel panicked
    CPU_LOCAL_WRITE(in_hardirq, false);
    CPU_LOCAL_WRITE(in_softirq, false);

    // @todo: ...
    ipi_broadcast_panic();

    uint64_t apic_id = arch_get_core_id();
    log_print_lockless(LOG_LEVEL_FAIL, "Kernel panic on core: %ld\n", apic_id);
}

[[clang::always_inline, noreturn]] static void panic_end() {
    log_print_lockless(LOG_LEVEL_FAIL, "\n\nmrrp mrrp meow meow mrrp. oops\n\n");
    log_print_lockless(LOG_LEVEL_FAIL, "                   _ |\\_\n");
    log_print_lockless(LOG_LEVEL_FAIL, "                   \\` ..\\\n");
    log_print_lockless(LOG_LEVEL_FAIL, "              __,.-\" =__Y=\n");
    log_print_lockless(LOG_LEVEL_FAIL, "            .\"        )\n");
    log_print_lockless(LOG_LEVEL_FAIL, "      _    /   ,    \\/\\_\n");
    log_print_lockless(LOG_LEVEL_FAIL, "     ((____|    )_-\\ \\_-\\`\n");
    log_print_lockless(LOG_LEVEL_FAIL, "     `-----'`-----` `--`\n");

    arch_shutdown_cpu();
}

[[noreturn]] void arch_panic(const char* fmt, ...) {
    panic_begin();

    log_print_lockless(LOG_LEVEL_FAIL, "Message: ");
    va_list args;
    va_start(args, fmt);
    log_vprint_lockless(LOG_LEVEL_FAIL, fmt, args);
    va_end(args);

    panic_end();
}

[[noreturn]] void arch_panic_int(arch_interrupt_frame_t* frame) {
    panic_begin();

    if(frame->scause <= 20) {
        log_print_lockless(LOG_LEVEL_FAIL, "%s (0x%lx)\n", g_name_table[frame->scause], frame->scause);
    } else {
        log_print_lockless(LOG_LEVEL_FAIL, "unknown (0x%lx)\n", frame->scause);
    }
    log_print_lockless(LOG_LEVEL_FAIL, "while in %s mode\n", ((frame->sstatus & (1 << 8)) == 0) ? "user" : "kernel");

    log_print_lockless(LOG_LEVEL_FAIL, "\n");
    for(int i = 0; i < 32; i += 4) {
        log_print_lockless(LOG_LEVEL_FAIL, "x%-2d = 0x%016lx, x%-2d = 0x%016lx, " "x%-2d = 0x%016lx, x%-2d = 0x%016lx\n", i, frame->registers[i], i + 1, frame->registers[i + 1], i + 2, frame->registers[i + 2], i + 3, frame->registers[i + 3]);
    }
    log_print_lockless(LOG_LEVEL_FAIL, "\n");

    log_print_lockless(LOG_LEVEL_FAIL, "sepc   = 0x%016lx, sstatus = 0x%016lx\n", frame->sepc, frame->sstatus);
    log_print_lockless(LOG_LEVEL_FAIL, "scause = 0x%016lx, stval   = 0x%016lx\n", frame->scause, frame->stval);

    panic_end();
}
