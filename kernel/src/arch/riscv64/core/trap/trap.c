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

    arch_panic_int(frame);
}

#pragma clang diagnostic pop
