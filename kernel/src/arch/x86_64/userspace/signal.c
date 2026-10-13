#include <arch/x86_64/interrupts/interrupt.h>
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

/// @brief Restores a userspace register state and returns to it via iretq
/// @note unlike sysret, iretq is able to restore every gpr.
[[noreturn]] void x86_64_signal_iret_to_user(arch_interrupt_idt_frame_t* frame);

#define SIGNAL_FRAME_RED_ZONE 128

void signal_arch_setup_frame(arch_interrupt_frame_t* frame, uapi_signal_info_t* signal_info, uapi_signal_handler_t* handler) {
    thread_t* thread = &CPU_LOCAL_GET_CURRENT_THREAD()->common;

    // @todo: sigaltstack

    uint64_t stack_pointer = frame->state->rsp;

    /// the handler runs in userspace and is free to clobber the interrupted frame's red zone
    stack_pointer -= SIGNAL_FRAME_RED_ZONE;

    /// siginfo_t
    stack_pointer -= sizeof(uapi_signal_info_t);
    stack_pointer = ALIGN_DOWN(stack_pointer, 16);
    userptr_t* signal_info_pointer = userptr_create(stack_pointer, sizeof(uapi_signal_info_t));

    /// ucontext_t
    stack_pointer -= sizeof(uapi_signal_ucontext_t);
    stack_pointer = ALIGN_DOWN(stack_pointer, 16);
    userptr_t* ucontext_pointer = userptr_create(stack_pointer, sizeof(uapi_signal_ucontext_t));

    /// the handler returns into the restorer, which should sigreturn, once the handler returns, the user stack pointer points at the ucontext
    stack_pointer -= 8;
    userptr_t* return_address_pointer = userptr_create(stack_pointer, sizeof(uint64_t));

    if(signal_info_pointer == nullptr || ucontext_pointer == nullptr || return_address_pointer == nullptr) {
        process_kill(thread->process, PROCESS_TERMINATED, UAPI_SIGNAL_SEGV);
        return;
    }

    uapi_signal_ucontext_t context = { 0 };
    context.sigmask = thread->signal.masked_signal_set;
    context.mcontext.pc = frame->state->rip;
    context.mcontext.flags = frame->state->rflags;
    context.mcontext.gregs[0] = frame->regs->rax;
    context.mcontext.gregs[1] = frame->regs->rbx;
    context.mcontext.gregs[2] = frame->regs->rcx;
    context.mcontext.gregs[3] = frame->regs->rdx;
    context.mcontext.gregs[4] = frame->regs->rsi;
    context.mcontext.gregs[5] = frame->regs->rdi;
    context.mcontext.gregs[6] = frame->regs->rbp;
    context.mcontext.gregs[7] = frame->state->rsp;
    context.mcontext.gregs[8] = frame->regs->r8;
    context.mcontext.gregs[9] = frame->regs->r9;
    context.mcontext.gregs[10] = frame->regs->r10;
    context.mcontext.gregs[11] = frame->regs->r11;
    context.mcontext.gregs[12] = frame->regs->r12;
    context.mcontext.gregs[13] = frame->regs->r13;
    context.mcontext.gregs[14] = frame->regs->r14;
    context.mcontext.gregs[15] = frame->regs->r15;

    uintptr_t signal_info_address = signal_info_pointer->user_ptr;
    uintptr_t ucontext_address = ucontext_pointer->user_ptr;
    uint64_t restorer = handler->restorer;

    bool ok = true;
    ok &= userptr_write(signal_info_pointer, signal_info);
    ok &= userptr_write(ucontext_pointer, &context);
    ok &= userptr_write(return_address_pointer, &restorer);

    heap_free(signal_info_pointer, sizeof(userptr_t));
    heap_free(ucontext_pointer, sizeof(userptr_t));
    heap_free(return_address_pointer, sizeof(userptr_t));

    if(!ok) {
        process_kill(thread->process, PROCESS_TERMINATED, UAPI_SIGNAL_SEGV);
        return;
    }

    /// Setup the handler to return back on userspace
    frame->state->rip = handler->handler;
    frame->state->rsp = stack_pointer;
    frame->regs->rdi = (uint64_t) signal_info->signal_number;
    frame->regs->rsi = signal_info_address;
    frame->regs->rdx = ucontext_address;
    frame->regs->rax = 0;
}

[[noreturn]] void signal_arch_enter_handler(arch_interrupt_frame_t* frame) {
    arch_interrupt_idt_frame_t idt_frame = { 0 };
    idt_frame.regs = *frame->regs;
    idt_frame.state = *frame->state;

    x86_64_signal_iret_to_user(&idt_frame);
}

[[noreturn]] void signal_arch_restore_frame(uapi_signal_ucontext_t* context) {
    arch_interrupt_idt_frame_t frame = { 0 };

    frame.regs.rax = context->mcontext.gregs[0];
    frame.regs.rbx = context->mcontext.gregs[1];
    frame.regs.rcx = context->mcontext.gregs[2];
    frame.regs.rdx = context->mcontext.gregs[3];
    frame.regs.rsi = context->mcontext.gregs[4];
    frame.regs.rdi = context->mcontext.gregs[5];
    frame.regs.rbp = context->mcontext.gregs[6];
    frame.regs.r8 = context->mcontext.gregs[8];
    frame.regs.r9 = context->mcontext.gregs[9];
    frame.regs.r10 = context->mcontext.gregs[10];
    frame.regs.r11 = context->mcontext.gregs[11];
    frame.regs.r12 = context->mcontext.gregs[12];
    frame.regs.r13 = context->mcontext.gregs[13];
    frame.regs.r14 = context->mcontext.gregs[14];
    frame.regs.r15 = context->mcontext.gregs[15];

    frame.state.rip = context->mcontext.pc;
    frame.state.cs = 0x2b; // lmode user code
    frame.state.rflags = context->mcontext.flags;
    frame.state.rsp = context->mcontext.gregs[7];
    frame.state.ss = 0x23; // lmode user data

    x86_64_signal_iret_to_user(&frame);
}
