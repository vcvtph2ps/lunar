#include <arch/riscv64/cpu_local.h>
#include <arch/riscv64/hardware/sbi.h>
#include <arch/riscv64/internal/csr.h>
#include <arch/riscv64/interrupts/interrupt.h>
#include <common/arch.h>
#include <common/assert.h>
#include <common/cpu_local.h>
#include <common/init.h>
#include <common/interrupts/dpc.h>
#include <common/interrupts/interrupt.h>
#include <common/interrupts/ipi.h>
#include <common/log.h>
#include <common/sched/sched.h>
#include <common/sync/spinlock.h>
#include <common/sync/wait_obj.h>
#include <common/userspace/signal.h>
#include <lib/helpers.h>
#include <memory/vm.h>
#include <stdint.h>

void riscv64_trap_handler(void);
void riscv64_trap_handler_vectored(void);

[[nodiscard]] arch_interrupt_state_t arch_interrupt_get_state() {
    uint64_t val = ARCH_CSR_READ(sstatus);
    return (arch_interrupt_state_t) { .enabled = (val & ARCH_CSR_SSTATUS_SIE) != 0 };
}

[[nodiscard]] arch_interrupt_state_t arch_interrupt_disable() {
    uint64_t val = ARCH_CSR_READ(sstatus);
    ARCH_CSR_CLEAR_BITS(sstatus, ARCH_CSR_SSTATUS_SIE);
    return (arch_interrupt_state_t) { .enabled = (val & ARCH_CSR_SSTATUS_SIE) != 0 };
}

[[nodiscard]] arch_interrupt_state_t arch_interrupt_enable() {
    uint64_t val = ARCH_CSR_READ(sstatus);
    ARCH_CSR_SET_BITS(sstatus, ARCH_CSR_SSTATUS_SIE);
    return (arch_interrupt_state_t) { .enabled = (val & ARCH_CSR_SSTATUS_SIE) != 0 };
}

void arch_interrupt_restore(arch_interrupt_state_t state) {
    if(state.enabled) {
        ARCH_CSR_SET_BITS(sstatus, ARCH_CSR_SSTATUS_SIE);
    } else {
        ARCH_CSR_CLEAR_BITS(sstatus, ARCH_CSR_SSTATUS_SIE);
    }
}

spinlock_no_int_t g_interrupt_handler_lock = SPINLOCK_NO_INT_INIT;
interrupt_handler_fn_t g_handlers[256] = {};
void* g_handler_contexts[256] = {};

void interrupt_set_hardirq_handler(uint8_t vector, interrupt_handler_fn_t handler, void* ctx) {
    arch_interrupt_state_t state = spinlock_noint_lock(&g_interrupt_handler_lock);
    assert(g_handlers[vector] == nullptr && "Interrupt handler already registered for vector");
    g_handlers[vector] = handler;
    g_handler_contexts[vector] = ctx;
    spinlock_noint_unlock(&g_interrupt_handler_lock, state);
}

static void softirq_handler(arch_interrupt_frame_t* frame, void* ctx) {
    (void) frame;
    dpc_queue(ctx);
}

void interrupt_set_softirq_handler(uint8_t vector, dpc_t* dpc) {
    arch_interrupt_state_t state = spinlock_noint_lock(&g_interrupt_handler_lock);
    assert(g_handlers[vector] == nullptr && "Interrupt handler already registered for vector");
    g_handlers[vector] = softirq_handler;
    g_handler_contexts[vector] = dpc;
    spinlock_noint_unlock(&g_interrupt_handler_lock, state);
}

static void signal_wake_obj_handler_dpc(void* ctx) {
    wait_obj_signal(ctx, 1);
}

void interrupt_set_waitobj_handler(uint8_t vector, wait_obj_t* wait_obj) {
    if(wait_obj->type == WAIT_OBJ_SEMAPHORE) {
        ASSERT_TODO();
    }

    dpc_t* item = dpc_create(signal_wake_obj_handler_dpc, wait_obj);
    interrupt_set_softirq_handler(vector, item);
}

void interrupt_init(uint32_t core_id) {
    (void) core_id;

    ARCH_CSR_WRITE(sie, ARCH_CSR_SIE_SEIE | ARCH_CSR_SIE_STIE | ARCH_CSR_SIE_SSIE);
    ARCH_CSR_CLEAR_BITS(sip, ARCH_CSR_SIP_SSIP | ARCH_CSR_SIP_STIP);

    ARCH_CSR_WRITE(stvec, (uint64_t) riscv64_trap_handler);
    if((ARCH_CSR_READ(stvec) & ARCH_CSR_STVEC_MODE_VECTORED) == 1)
        ARCH_CSR_WRITE(stvec, (uint64_t) riscv64_trap_handler_vectored | ARCH_CSR_STVEC_MODE_VECTORED);
}

void interrupt_set_usermode_stack(uint64_t stack_pointer) {
    riscv64_thread_t* thread = CPU_LOCAL_GET_CURRENT_THREAD();
    if(thread != nullptr) {
        // trap.S switches to this stack when taking a user mode trap
        thread->syscall_storage = stack_pointer;
    }
}

void arch_send_ipi(uint32_t core_id) {
    if(g_init_boot_info == nullptr || core_id >= g_init_boot_info->core_count) {
        return;
    }

    uint64_t hart_id = g_init_boot_info->cpus[core_id].arch_id;
    arch_sbi_ret_t ret = arch_sbi_ecall2(ARCH_SBI_EXT_IPI, ARCH_SBI_IPI_SEND, 1, (long) hart_id);
    if(ret.error != 0) {
        LOG_WARN("Failed to send IPI to core %u (hart %lu): %ld\n", core_id, hart_id, ret.error);
    }
}

void arch_send_panic_ipi(uint32_t core_id) {
    if(g_init_boot_info == nullptr || core_id >= g_init_boot_info->core_count) {
        return;
    }

    // riscv only has a single interrupt cause for ipis
    ATOMIC_STORE(&cpu_local_get_other(core_id)->panic_ipi_pending, true, ATOMIC_RELEASE);
    arch_send_ipi(core_id);
}

static void ipi_interrupt_handler(arch_interrupt_frame_t* frame, void* ctx) {
    (void) frame;
    (void) ctx;

    if(ATOMIC_XCHG(&CPU_LOCAL_GET_SELF()->panic_ipi_pending, false, ATOMIC_ACQ_REL)) {
        ipi_handle_panic();
    }

    ipi_handle();

    dpc_t* free_item = CPU_LOCAL_READ(ipi.dpc_free_item);
    if(free_item != nullptr) {
        dpc_queue(free_item);
    }
}

static void ipi_handle_deferred(void* ctx) {
    (void) ctx;
    ipi_reclaim();
}

void ipi_init(uint32_t core_id) {
    if(INIT_CORE_IS_BSP(core_id)) {
        interrupt_set_hardirq_handler(ARCH_INTERRUPT_CAUSE_SSI, ipi_interrupt_handler, nullptr);
    }

    CPU_LOCAL_GET_SELF()->ipi.dpc_free_item = dpc_create(ipi_handle_deferred, nullptr);
}

static void dispatch_handler(uint32_t vector, arch_interrupt_frame_t* frame) {
    if(vector < sizeof(g_handlers) / sizeof(g_handlers[0]) && g_handlers[vector] != nullptr) {
        g_handlers[vector](frame, g_handler_contexts[vector]);
    } else {
        LOG_WARN("No handler registered for interrupt cause 0x%02x\n", vector);
    }
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-prototypes"

void riscv64_dispatch_interrupt(arch_interrupt_frame_t* frame) {
    uint64_t cause = frame->scause & ~(1ull << 63);
    bool is_user = (frame->sstatus & ARCH_CSR_SSTATUS_SPP) == 0;

    bool is_threaded = CPU_LOCAL_READ(scheduler.threaded);
    bool is_outmost_handler = false;

    if(is_threaded) {
        riscv64_thread_t* current = CPU_LOCAL_GET_CURRENT_THREAD();
        is_outmost_handler = !current->common.in_interrupt_handler;
        if(is_outmost_handler) {
            current->common.in_interrupt_handler = true;
        }

        sched_preempt_disable();
        dpc_disable();
    }

    // clear the sources that are ours to acknowledge. external interrupts are completed by the interrupt controller
    switch(cause) {
        case ARCH_INTERRUPT_CAUSE_SSI: ARCH_CSR_CLEAR_BITS(sip, ARCH_CSR_SIP_SSIP); break;
        case ARCH_INTERRUPT_CAUSE_STI: ARCH_CSR_CLEAR_BITS(sip, ARCH_CSR_SIP_STIP); break;
        default:                       break;
    }

    CPU_LOCAL_WRITE(in_hardirq, true);
    dispatch_handler((uint32_t) cause, frame);
    CPU_LOCAL_WRITE(in_hardirq, false);

    if(is_threaded) {
        CPU_LOCAL_WRITE(in_softirq, false);
        (void) arch_interrupt_enable();
        dpc_enable();
        sched_preempt_enable();

        if(is_outmost_handler) {
            CPU_LOCAL_GET_CURRENT_THREAD()->common.in_interrupt_handler = false;
        }

        if(is_user) {
            vm_fault_process_pending();
            signal_deliver_pending(frame);
        }
    }
}
#pragma clang diagnostic pop
