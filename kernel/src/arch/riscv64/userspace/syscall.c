#include <arch/riscv64/interrupts/interrupt.h>
#include <common/assert.h>
#include <common/interrupts/interrupt.h>
#include <common/userspace/signal.h>
#include <common/userspace/syscall.h>
#include <stddef.h>

#define RISCV64_REG_SP 2
#define RISCV64_REG_A0 10
#define RISCV64_REG_A1 11
#define RISCV64_REG_A2 12
#define RISCV64_REG_A3 13
#define RISCV64_REG_A4 14
#define RISCV64_REG_A5 15
#define RISCV64_REG_A7 17

syscall_ret_t riscv64_dispatch_syscall(arch_interrupt_regs_t* regs);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-prototypes"

void arch_syscall_init() {
    /// @note nothing to do... riscv <3
}

syscall_ret_t riscv64_dispatch_syscall(arch_interrupt_regs_t* regs) {
    syscall_args_t args;
    args.syscall_nr = regs->registers[RISCV64_REG_A7];
    args.args[0] = regs->registers[RISCV64_REG_A0];
    args.args[1] = regs->registers[RISCV64_REG_A1];
    args.args[2] = regs->registers[RISCV64_REG_A2];
    args.args[3] = regs->registers[RISCV64_REG_A3];
    args.args[4] = regs->registers[RISCV64_REG_A4];
    args.args[5] = regs->registers[RISCV64_REG_A5];
    args.user_rsp = regs->registers[RISCV64_REG_SP];
    args.regs = regs;

    /// sepc points at ecall, so we must skip passed it
    regs->sepc += 4;

    syscall_ret_t ret = syscall_dispatch(&args);

    /// preserve the return value so that a signal delivered on the way out restores it
    regs->registers[RISCV64_REG_A0] = ret.value;
    regs->registers[RISCV64_REG_A1] = ret.is_error;

    /// If a signal is pending set up its handler and return to it directly
    if(signal_deliver_pending(regs)) {
        signal_arch_enter_handler(regs);
    }

    return ret;
}

#pragma clang diagnostic pop
