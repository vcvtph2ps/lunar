#include <arch/x86_64/internal/gdt.h>
#include <arch/x86_64/internal/msr.h>
#include <common/interrupts/interrupt.h>
#include <common/userspace/signal.h>
#include <common/userspace/syscall.h>
#include <stddef.h>

void x86_64_handle_syscall();

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-prototypes"

void arch_syscall_init() {
    uint64_t efer = arch_msr_read(ARCH_MSR_EFER);
    // set IA32_EFER.SCE
    efer |= (1 << 0);
    arch_msr_write(ARCH_MSR_EFER, efer);

    /*
     * on syscall the cpu will take the value in IA32_LSTAR[47:32], and load it into CS, then adds 8 to it and loads it into SS
     * on sysret the cpu will take the value in IA32_LSTAR[63:48], add 8 and load it into SS
     * the value of CS depends on the operand size of the sysret instruction
     * for 32-bit, it takes the value as is, and loads it into cs
     * for 64-bit, it takes the value adds 16, and loads it into cs
     */
    uint64_t kernel_selector = offsetof(arch_gdt_t, kernel_code);
    uint64_t user_selector = offsetof(arch_gdt_t, user_code_32) | 3;
    uint64_t star = (user_selector << 48) | (kernel_selector << 32);
    arch_msr_write(ARCH_MSR_STAR, star);

    // ~0x2 clears every bit in rflags except for bit 1, that is inexplicably required to be 1
    arch_msr_write(ARCH_MSR_SFMASK, ~0x2);
    arch_msr_write(ARCH_MSR_LSTAR, (uint64_t) x86_64_handle_syscall);
}

// @note: user rip is in rcx, user rflags is in r11
typedef struct {
    uint64_t user_rsp;
    arch_interrupt_regs_t regs;
} x86_64_syscall_frame_t;

syscall_ret_t x86_64_dispatch_syscall(x86_64_syscall_frame_t* frame) {
    uint64_t syscall_nr = frame->regs.rax;

    syscall_args_t args;
    args.syscall_nr = syscall_nr;
    args.args[0] = frame->regs.rdi;
    args.args[1] = frame->regs.rsi;
    args.args[2] = frame->regs.rdx;
    args.args[3] = frame->regs.r10;
    args.args[4] = frame->regs.r8;
    args.args[5] = frame->regs.r9;
    args.user_rsp = frame->user_rsp;
    args.regs = &frame->regs;

    syscall_ret_t ret = syscall_dispatch(&args);

    /// Preserve the return value so that a signal delivered on the way out restores it
    frame->regs.rax = ret.value;
    frame->regs.rdx = ret.is_error;

    arch_interrupt_sregs_t state = {
        .rip = frame->regs.rcx,
        .cs = 0x2b, // lmode user code
        .rflags = frame->regs.r11,
        .rsp = frame->user_rsp,
        .ss = 0x23, // lmode user data
    };
    arch_interrupt_frame_t interrupt_frame = {
        .regs = &frame->regs,
        .state = &state,
        .is_user = true,
    };

    /// If a signal is pending set up its handler and return to it via iretq instead of sysret
    if(signal_deliver_pending(&interrupt_frame)) {
        signal_arch_enter_handler(&interrupt_frame);
    }

    return ret;
}
#pragma clang diagnostic pop
