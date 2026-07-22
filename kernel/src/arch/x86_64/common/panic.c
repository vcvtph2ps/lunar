#include <arch/x86_64/cpu_local.h>
#include <arch/x86_64/internal/cr.h>
#include <arch/x86_64/interrupts/interrupt.h>
#include <common/arch.h>
#include <common/interrupts/ipi.h>
#include <common/log.h>
#include <lib/helpers.h>
#include <memory/vm.h>
#include <stdarg.h>

static const char* g_name_table[22] = { "Divide Error",
                                        "Debug Exception",
                                        "NMI Interrupt",
                                        "Breakpoint",
                                        "Overflow",
                                        "BOUND Range Exceeded",
                                        "Undefined Opcode",
                                        "Device Not Available (No Math Coprocessor)",
                                        "Double Fault",
                                        "Coprocessor Segment Overrun",
                                        "Invalid TSS",
                                        "Segment Not Present",
                                        "Stack-Segment Fault",
                                        "General Protection ",
                                        "Page Fault",
                                        "Reserved",
                                        "x87 FPU Floating-Point Error",
                                        "Alignment Check",
                                        "Machine Check",
                                        "SIMD Floating-Point Exception",
                                        "Virtualization Exception",
                                        "Control Protection Exception" };

ATOMIC uint32_t g_panicking_core = 0xffffffff;

[[clang::always_inline]] static void panic_begin() {
    __asm__ volatile("cli;mfence;lfence" ::: "memory");
    uint32_t core_id = CPU_LOCAL_READ(core_id);

    uint32_t old_value = ATOMIC_XCHG(&g_panicking_core, core_id, ATOMIC_SEQ_CST);

    if(old_value == core_id) {
        log_print_lockless(LOG_LEVEL_FAIL, "Kernel panic on core: %d (recursive panic)\n", core_id);
        while(1) { arch_wait_for_interrupt(); }
    }

    if(old_value != 0xffffffff) {
        while(1) { arch_wait_for_interrupt(); }
    }

    // @note: we set these to false, we need to be able to grab any kind of lock. and it doesn't matter anyway the kernel panicked
    CPU_LOCAL_WRITE(in_hardirq, false);
    CPU_LOCAL_WRITE(in_softirq, false);

    ipi_broadcast((ipi_message_t) { .type = IPI_HALT });

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

    while(1) { asm volatile("pause;hlt"); }
    __builtin_unreachable();
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

    if(frame->vector == 0x0E) {
        uint8_t page_protection_violation = ((frame->error & (1 << 0)) > 0);
        uint8_t write_access = ((frame->error & (1 << 1)) > 0);
        uint8_t user_mode = ((frame->error & (1 << 2)) > 0);
        uint8_t reserved_bit = ((frame->error & (1 << 3)) > 0);
        uint8_t instruction_fetch = ((frame->error & (1 << 4)) > 0);
        uint8_t protection_key = ((frame->error & (1 << 5)) > 0);
        uint8_t shadow_stack = ((frame->error & (1 << 6)) > 0);
        uint8_t sgx = ((frame->error & (1 << 15)) > 0);
        log_print_lockless(
            LOG_LEVEL_FAIL,
            "Page fault @ 0x%016lx [ppv=%d, write=%d, ring3=%d, resv=%d, fetch=%d, pk=%d, ss=%d, sgx=%d, uap=%d]\n",
            frame->interrupt_data,
            page_protection_violation,
            write_access,
            user_mode,
            reserved_bit,
            instruction_fetch,
            protection_key,
            shadow_stack,
            sgx,
            0
        );

        if(reserved_bit) { log_print_lockless(LOG_LEVEL_FAIL, "Reserved bit in page table entry\n"); }
        if(protection_key) {
            log_print_lockless(LOG_LEVEL_FAIL, "Protection key violation\n");
        } else {
            const char* who = user_mode ? "User Process" : "Kernel";
            const char* access = write_access ? "write" : "read";
            if(instruction_fetch) { access = "execute"; }

            const char* reason = page_protection_violation ? "non-accessable" : "non-present";
            if(instruction_fetch) { reason = "non-executable"; }

            log_print_lockless(LOG_LEVEL_FAIL, "%s tried to %s a %s page\n", who, access, reason);
        }
    } else if(frame->vector == 0x0D) {
        if(frame->error == 0) {
            log_print_lockless(LOG_LEVEL_FAIL, "General Protection Fault (0x%lx | no error code)\n", frame->vector);
        } else {
            log_print_lockless(LOG_LEVEL_FAIL, "General Protection Fault (0x%lx | 0x%lx)", frame->vector, frame->error);
            if(frame->error & 0x1) { log_print_lockless(LOG_LEVEL_FAIL, "  [external event]"); }
            if(frame->error & 0x2) {
                log_print_lockless(LOG_LEVEL_FAIL, " [idt]");
            } else if(frame->error & 0x4) {
                log_print_lockless(LOG_LEVEL_FAIL, " [ldt]");
            } else {
                log_print_lockless(LOG_LEVEL_FAIL, " [gdt]");
            }
            log_print_lockless(LOG_LEVEL_FAIL, " [index=0x%lx)\n", (frame->error & 0xFFF8) >> 4);
        }
    } else if(frame->vector <= 21) {
        log_print_lockless(LOG_LEVEL_FAIL, "%s (0x%lx | %ld)\n", g_name_table[frame->vector], frame->vector, frame->error);
    } else {
        log_print_lockless(LOG_LEVEL_FAIL, "unknown (0x%lx | %ld)\n", frame->vector, frame->error);
    }
    log_print_lockless(LOG_LEVEL_FAIL, "while in %s mode\n", frame->is_user ? "user" : "kernel");

    log_print_lockless(LOG_LEVEL_FAIL, "\n");
    arch_interrupt_regs_t* regs = frame->regs;
    log_print_lockless(LOG_LEVEL_FAIL, "rax = 0x%016lx, rbx = 0x%016lx\n", regs->rax, regs->rbx);
    log_print_lockless(LOG_LEVEL_FAIL, "rcx = 0x%016lx, rdx = 0x%016lx\n", regs->rcx, regs->rdx);
    log_print_lockless(LOG_LEVEL_FAIL, "rsi = 0x%016lx, rdi = 0x%016lx\n", regs->rsi, regs->rdi);
    log_print_lockless(LOG_LEVEL_FAIL, "r8  = 0x%016lx, r9  = 0x%016lx\n", regs->r8, regs->r9);
    log_print_lockless(LOG_LEVEL_FAIL, "r10 = 0x%016lx, r11 = 0x%016lx\n", regs->r10, regs->r11);
    log_print_lockless(LOG_LEVEL_FAIL, "r12 = 0x%016lx, r13 = 0x%016lx\n", regs->r12, regs->r13);
    log_print_lockless(LOG_LEVEL_FAIL, "r14 = 0x%016lx, r15 = 0x%016lx\n", regs->r14, regs->r15);

    log_print_lockless(LOG_LEVEL_FAIL, "\n");

    log_print_lockless(LOG_LEVEL_FAIL, "cs  = 0x%016lx, rip = 0x%016lx\n", frame->state->cs, frame->state->rip);
    log_print_lockless(LOG_LEVEL_FAIL, "ss  = 0x%016lx, rsp = 0x%016lx\n", frame->state->ss, frame->state->rsp);
    log_print_lockless(LOG_LEVEL_FAIL, "rbp = 0x%016lx, rflags = 0x%016lx\n", frame->regs->rbp, frame->state->rflags);

    log_print_lockless(LOG_LEVEL_FAIL, "error = 0x%016lx, interrupt data = 0x%016lx\n", frame->error, frame->interrupt_data);

    uint64_t cr0 = arch_cr_read_cr0();
    uint64_t cr2 = arch_cr_read_cr2();
    uint64_t cr3 = arch_cr_read_cr3();
    uint64_t cr4 = arch_cr_read_cr4();
    uint64_t cr8 = arch_cr_read_cr8();

    uint64_t kernel_cr3 = g_vm_global_address_space->ptm.top_level_page_table;

    log_print_lockless(LOG_LEVEL_FAIL, "\n");
    log_print_lockless(LOG_LEVEL_FAIL, "cr0 = 0x%016lx\n", cr0);
    log_print_lockless(LOG_LEVEL_FAIL, "cr2 = 0x%016lx [faulting address]\n", cr2);
    log_print_lockless(LOG_LEVEL_FAIL, "cr3 = 0x%016lx", cr3);
    if(cr3 == kernel_cr3) {
        log_print_lockless(LOG_LEVEL_FAIL, " [main kernel page table]\n");
    } else {
        log_print_lockless(LOG_LEVEL_FAIL, " [other page table]\n");
    }
    log_print_lockless(LOG_LEVEL_FAIL, "cr4 = 0x%016lx [todo]\n", cr4);
    log_print_lockless(LOG_LEVEL_FAIL, "cr8 = 0x%016lx [tpl=%ld]\n", cr8, cr8);

    panic_end();
}
