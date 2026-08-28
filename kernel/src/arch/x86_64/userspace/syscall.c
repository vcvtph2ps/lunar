#include <arch/x86_64/internal/msr.h>
#include <common/userspace/syscall.h>

void x86_64_handle_syscall();

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-prototypes"

void arch_syscall_init() {
    uint64_t efer = arch_msr_read(ARCH_MSR_EFER);
    efer |= (1 << 0);
    arch_msr_write(ARCH_MSR_EFER, efer);

    uint64_t star = ((uint64_t) (0x18 | 3) << 48) | ((uint64_t) 0x08 << 32);
    arch_msr_write(ARCH_MSR_STAR, star);

    arch_msr_write(ARCH_MSR_LSTAR, (uint64_t) x86_64_handle_syscall);
    arch_msr_write(ARCH_MSR_SFMASK, ~0x2);
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
    args.arg1 = frame->regs.rdi;
    args.arg2 = frame->regs.rsi;
    args.arg3 = frame->regs.rdx;
    args.arg4 = frame->regs.r10;
    args.arg5 = frame->regs.r8;
    args.arg6 = frame->regs.r9;
    args.arch_frame = frame;

    return syscall_dispatch(&args);
}
#pragma clang diagnostic pop
