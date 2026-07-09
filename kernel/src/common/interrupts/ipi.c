#include <common/arch.h>
#include <common/cpu_local.h>
#include <common/init.h>
#include <common/interrupts/ipi.h>
#include <common/sched/sched.h>
#include <lib/helpers.h>
#include <lib/string.h>
#include <memory/pmm.h>
#include <memory/ptm.h>

void ipi_send(uint32_t core_id, ipi_message_t message) {
    sched_preempt_disable();

    ipi_request_t* request = (ipi_request_t*) PTM_TO_HHDM(pmm_alloc_page(PMM_FLAG_ZERO | PMM_FLAG_PANIC));
    memcpy(&request->message, &message, sizeof(ipi_message_t));
    ATOMIC_STORE(&request->next, nullptr, __ATOMIC_SEQ_CST);

    arch_cpu_local_t* cpu_local = cpu_local_get_other(core_id);

    ipi_request_t* prev_head = ATOMIC_XCHG(&cpu_local->ipi_queue, request, __ATOMIC_SEQ_CST);
    ATOMIC_STORE(&request->next, prev_head, __ATOMIC_SEQ_CST);

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
    ipi_request_t* request = ATOMIC_XCHG(&cpu_local->ipi_queue, nullptr, __ATOMIC_SEQ_CST);

    if(request == nullptr) {
        sched_preempt_enable();
        return false;
    }

    memcpy(message, &request->message, sizeof(ipi_message_t));
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
