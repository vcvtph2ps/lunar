#include <arch/x86_64/interrupts/interrupt.h>
#include <common/arch.h>
#include <common/cpu_local.h>
#include <common/init.h>
#include <common/interrupts/ipi.h>
#include <common/sched/sched.h>
#include <common/sync/spinlock.h>
#include <lib/helpers.h>
#include <lib/string.h>
#include <memory/pmm.h>
#include <memory/ptm.h>

void ipi_send(uint32_t core_id, ipi_message_t message) {
    sched_preempt_disable();
    arch_cpu_local_t* cpu_local = cpu_local_get_other(core_id);
    if(!cpu_local->online) {
        sched_preempt_enable();
        return;
    }

    ipi_request_t* request = (ipi_request_t*) PTM_TO_HHDM(pmm_alloc_page(PMM_FLAG_ZERO | PMM_FLAG_PANIC));
    memcpy(&request->message, &message, sizeof(ipi_message_t));
    ATOMIC_STORE(&request->next, nullptr, ATOMIC_SEQ_CST);

    arch_interrupt_state_t state = spinlock_noint_lock(&cpu_local->ipi.lock);

    ipi_request_t* prev_head = ATOMIC_XCHG(&cpu_local->ipi.queue, request, ATOMIC_SEQ_CST);
    ATOMIC_STORE(&request->next, prev_head, ATOMIC_SEQ_CST);

    spinlock_noint_unlock(&cpu_local->ipi.lock, state);

    arch_send_ipi(core_id);
    sched_preempt_enable();
}

void ipi_broadcast(ipi_message_t message) {
    sched_preempt_disable();

    uint64_t self = CPU_LOCAL_READ(core_id);
    for(uint32_t i = 0; i < g_init_boot_info->core_count; i++) {
        if(i == self) continue;
        ipi_send(i, message);
    }

    sched_preempt_enable();
}

bool ipi_pop(ipi_message_t* message) {
    sched_preempt_disable();

    arch_cpu_local_t* cpu_local = CPU_LOCAL_GET_SELF();

    arch_interrupt_state_t state = spinlock_noint_lock(&cpu_local->ipi.lock);
    ipi_request_t* request = ATOMIC_XCHG(&cpu_local->ipi.queue, nullptr, ATOMIC_SEQ_CST);
    if(request == nullptr) {
        spinlock_noint_unlock(&cpu_local->ipi.lock, state);
        sched_preempt_enable();
        return false;
    }

    ATOMIC_STORE(&cpu_local->ipi.queue, ATOMIC_LOAD(&request->next, ATOMIC_SEQ_CST), ATOMIC_SEQ_CST);

    spinlock_noint_unlock(&cpu_local->ipi.lock, state);
    memcpy(message, &request->message, sizeof(ipi_message_t));

    // @note: this can deadlock :/
    pmm_free_page((uintptr_t) PTM_FROM_HHDM(request));

    sched_preempt_enable();
    return true;
}

void ipi_handle(ipi_message_t message) {
    switch(message.type) {
        case IPI_TLB_FLUSH: ptm_flush_tlb(message.tlb_flush.vaddr, message.tlb_flush.length); break;
        case IPI_HALT:
            while(1) {
                (void) arch_interrupt_disable();
                arch_wait_for_interrupt();
            }
        default: arch_panic("Received unknown IPI type %d on core %d", message.type, CPU_LOCAL_READ(core_id));
    }
}
