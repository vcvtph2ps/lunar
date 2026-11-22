#include <arch/riscv64/internal/csr.h>
#include <arch/riscv64/interrupts/interrupt.h>
#include <common/arch.h>
#include <common/cpu_local.h>
#include <common/interrupts/interrupt.h>
#include <common/log.h>
#include <common/sched/process.h>
#include <common/userspace/signal.h>
#include <memory/ptm.h>
#include <memory/vm.h>
#include <stdint.h>

#define SCAUSE_INTERRUPT_BIT (1ull << 63)
#define SCAUSE_ECALL_FROM_USER 8
#define SCAUSE_INSTRUCTION_PAGE_FAULT 12
#define SCAUSE_LOAD_PAGE_FAULT 13
#define SCAUSE_STORE_PAGE_FAULT 15

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-prototypes"

void riscv64_dispatch_interrupt(arch_interrupt_frame_t* frame);
void riscv64_dispatch_syscall(arch_interrupt_regs_t* regs);

/// @brief Convert exception cause to the signal it should raise
/// @return The signal number, or -1 if the exception is fatal
static int exception_cause_to_signal(uint64_t cause) {
    switch(cause) {
        case 0:  return UAPI_SIGNAL_BUS; // instruction address misaligned
        case 1:  return UAPI_SIGNAL_SEGV; // instruction access fault
        case 2:  return UAPI_SIGNAL_ILL; // illegal instruction
        case 3:  return UAPI_SIGNAL_TRAP; // breakpoint
        case 4:  return UAPI_SIGNAL_BUS; // load address misaligned
        case 5:  return UAPI_SIGNAL_SEGV; // load access fault
        case 6:  return UAPI_SIGNAL_BUS; // store/AMO address misaligned
        case 7:  return UAPI_SIGNAL_SEGV; // store/AMO access fault
        case 18: return UAPI_SIGNAL_SEGV; // software check
        case 20: return UAPI_SIGNAL_SEGV; // instruction guest-page fault
        case 21: return UAPI_SIGNAL_SEGV; // load guest-page fault
        case 22: return UAPI_SIGNAL_ILL; // virtual instruction
        case 23: return UAPI_SIGNAL_SEGV; // store/AMO guest-page fault
        default: return -1; // ecall from S-mode, hardware error, ...
    }
}

/// @brief Raises @p signal_number on the current user thread so it is delivered on the way out of the trap
static void raise_user_signal(arch_interrupt_frame_t* frame, int signal_number) {
    thread_t* thread = &CPU_LOCAL_GET_CURRENT_THREAD()->common;
    uapi_signal_info_t info = { .signal_number = signal_number, .code = UAPI_SIGNAL_KERNEL, .addr = (void*) frame->stval };
    signal_thread(thread, signal_number, &info);

    (void) signal_userspace_prepare_return(frame);
}

static void handle_page_fault(arch_interrupt_frame_t* frame, uint64_t cause) {
    if((frame->sstatus & ARCH_CSR_SSTATUS_SPP) != 0) {
        arch_panic_int(frame);
    }

    vm_fault_reason_t reason = VM_FAULT_NOT_PRESENT;
    if(cause == SCAUSE_STORE_PAGE_FAULT) {
        thread_t* thread = &CPU_LOCAL_GET_CURRENT_THREAD()->common;
        uintptr_t physical;
        if(thread->process != nullptr && ptm_physical(thread->process->address_space, frame->stval, &physical)) {
            reason = VM_FAULT_WRITE_TO_RO;
        }
    }

    if(!vm_handle_page_fault(frame->stval, reason)) {
        arch_panic_int(frame);
    }

    (void) signal_userspace_prepare_return(frame);
}

void riscv64_trap_dispatch(arch_interrupt_frame_t* frame) {
    bool is_interrupt = (frame->scause & SCAUSE_INTERRUPT_BIT) != 0;
    uint64_t cause = frame->scause & ~SCAUSE_INTERRUPT_BIT;
    bool is_user = (frame->sstatus & ARCH_CSR_SSTATUS_SPP) == 0;

    if(is_interrupt) {
        riscv64_dispatch_interrupt(frame);
        return;
    }

    if(cause == SCAUSE_ECALL_FROM_USER && is_user) {
        riscv64_dispatch_syscall(frame);
        return;
    }

    if(cause == SCAUSE_INSTRUCTION_PAGE_FAULT || cause == SCAUSE_LOAD_PAGE_FAULT || cause == SCAUSE_STORE_PAGE_FAULT) {
        handle_page_fault(frame, cause);
        return;
    }

    if(is_user) {
        int signal_number = exception_cause_to_signal(cause);
        if(signal_number < 0) {
            arch_panic_int(frame);
        }

        raise_user_signal(frame, signal_number);
        return;
    }

    arch_panic_int(frame);
}

#pragma clang diagnostic pop
