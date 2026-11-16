#include <arch/riscv64/internal/csr.h>
#include <arch/riscv64/interrupts/interrupt.h>
#include <common/assert.h>
#include <common/cpu_local.h>
#include <common/interrupts/interrupt.h>
#include <common/sched/process.h>
#include <common/sched/thread.h>
#include <common/userspace/signal.h>
#include <common/userspace/userptr.h>
#include <lib/string.h>
#include <lib/types.h>
#include <memory/heap.h>
#include <uapi/signal.h>

[[noreturn]] void riscv64_signal_sret_to_user(arch_interrupt_regs_t* regs);

#define RISCV64_REG_RA 1
#define RISCV64_REG_SP 2
#define RISCV64_REG_A0 10
#define RISCV64_REG_A1 11
#define RISCV64_REG_A2 12

void signal_arch_setup_frame(arch_interrupt_frame_t* frame, uapi_signal_info_t* signal_info, uapi_signal_handler_t* handler) {
    thread_t* thread = &CPU_LOCAL_GET_CURRENT_THREAD()->common;

    // @todo: sigaltstack

    uint64_t stack_pointer = frame->registers[RISCV64_REG_SP];

    /// siginfo_t
    stack_pointer -= sizeof(uapi_signal_info_t);
    stack_pointer = ALIGN_DOWN(stack_pointer, 16);
    userptr_t* signal_info_pointer = userptr_create(stack_pointer, sizeof(uapi_signal_info_t));

    /// ucontext_t
    stack_pointer -= sizeof(uapi_signal_ucontext_t);
    stack_pointer = ALIGN_DOWN(stack_pointer, 16);
    userptr_t* ucontext_pointer = userptr_create(stack_pointer, sizeof(uapi_signal_ucontext_t));

    if(signal_info_pointer == nullptr || ucontext_pointer == nullptr) {
        process_kill(thread->process, PROCESS_TERMINATED, UAPI_SIGNAL_SEGV);
        return;
    }

    uapi_signal_ucontext_t context = { 0 };
    context.sigmask = thread->signal.masked_signal_set;
    context.mcontext.pc = frame->sepc;
    context.mcontext.flags = frame->sstatus;
    /// x0 is hardwired to zero and is never saved by the trap entry
    for(size_t i = 1; i < 32; i++) {
        context.mcontext.gregs[i] = frame->registers[i];
    }

    uintptr_t signal_info_address = signal_info_pointer->user_ptr;
    uintptr_t ucontext_address = ucontext_pointer->user_ptr;

    bool ok = true;
    ok &= userptr_write_full(signal_info_pointer, signal_info);
    ok &= userptr_write_full(ucontext_pointer, &context);

    heap_free(signal_info_pointer, sizeof(userptr_t));
    heap_free(ucontext_pointer, sizeof(userptr_t));

    if(!ok) {
        process_kill(thread->process, PROCESS_TERMINATED, UAPI_SIGNAL_SEGV);
        return;
    }

    /// the handler is entered like a regular function call and returns straight into the restorer, which expects the ucontext at the stack pointer.
    frame->sepc = handler->handler;
    frame->registers[RISCV64_REG_RA] = handler->restorer;
    frame->registers[RISCV64_REG_SP] = stack_pointer;
    frame->registers[RISCV64_REG_A0] = (uint64_t) signal_info->signal_number;
    frame->registers[RISCV64_REG_A1] = signal_info_address;
    frame->registers[RISCV64_REG_A2] = ucontext_address;
}

[[noreturn]] void signal_arch_enter_handler(arch_interrupt_frame_t* frame) {
    riscv64_signal_sret_to_user(frame);
    ASSERT_UNREACHABLE();
}

[[noreturn]] void signal_arch_restore_frame(uapi_signal_ucontext_t* context) {
    arch_interrupt_regs_t regs = { 0 };
    /// start from one since x0 is hardwired to zero
    for(size_t i = 1; i < 32; i++) {
        regs.registers[i] = context->mcontext.gregs[i];
    }
    regs.sepc = context->mcontext.pc;

    /// only trust the floating point state
    uint64_t sstatus = context->mcontext.flags;
    sstatus &= ~(ARCH_CSR_SSTATUS_SPP | ARCH_CSR_SSTATUS_SIE | ARCH_CSR_SSTATUS_SUM);
    sstatus |= ARCH_CSR_SSTATUS_SPIE;
    regs.sstatus = sstatus;

    riscv64_signal_sret_to_user(&regs);
    ASSERT_UNREACHABLE();
}
