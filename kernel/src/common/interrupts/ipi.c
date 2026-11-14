#include <common/arch.h>
#include <common/cpu_local.h>
#include <common/init.h>
#include <common/interrupts/interrupt.h>
#include <common/interrupts/ipi.h>
#include <common/sched/sched.h>
#include <common/sync/spinlock.h>
#include <lib/atomic_stack.h>
#include <lib/helpers.h>
#include <lib/list.h>
#include <lib/string.h>
#include <memory/heap.h>
#include <memory/pmm.h>
#include <memory/ptm.h>

void ipi_send(uint32_t core_id, ipi_message_t message) {
    sched_preempt_disable();
    arch_cpu_local_t* cpu_local = cpu_local_get_other(core_id);
    if(!cpu_local->online) {
        sched_preempt_enable();
        return;
    }

    // @todo: figure out something better than just using a whole physical page here....
    ipi_request_t* request = (ipi_request_t*) PTM_TO_HHDM(pmm_alloc_page(PMM_FLAG_PANIC));
    memory_copy(&request->message, &message, sizeof(ipi_message_t));
    atomic_stack_push(&cpu_local->ipi.queue, &request->node);

    arch_send_ipi(core_id);
    sched_preempt_enable();
}

void ipi_broadcast(ipi_message_t message) {
    sched_preempt_disable();

    uint64_t self = CPU_LOCAL_READ(core_id);
    for(uint32_t i = 0; i < g_init_boot_info->core_count; i++) {
        if(i == self)
            continue;
        ipi_send(i, message);
    }

    sched_preempt_enable();
}

static void process_ipi(ipi_message_t message) {
    switch(message.type) {
        case IPI_TLB_FLUSH:     ptm_flush_tlb(message.tlb_flush.vaddr, message.tlb_flush.length); break;
        case IPI_TLB_FLUSH_ALL: ptm_flush_tlb_all(); break;
        case IPI_HALT:          arch_halt_cpu();
        default:                arch_panic("Received unknown IPI type %d on core %d\n", message.type, CPU_LOCAL_READ(core_id));
    }
}

void ipi_handle() {
    sched_preempt_disable();

    stack_t stack = atomic_stack_take_all(&CPU_LOCAL_GET_SELF()->ipi.queue);
    while(true) {
        stack_node_t* node = stack_pop(&stack);
        if(node == nullptr)
            break;

        ipi_request_t* request = CONTAINER_OF((atomic_stack_node_t*) node, ipi_request_t, node);
        process_ipi(request->message);
        atomic_stack_push(&CPU_LOCAL_GET_SELF()->ipi.free_queue, &request->node);
    }

    sched_preempt_enable();
}

void ipi_reclaim() {
    sched_preempt_disable();

    stack_t stack = atomic_stack_take_all(&CPU_LOCAL_GET_SELF()->ipi.free_queue);
    while(true) {
        stack_node_t* node = stack_pop(&stack);
        if(node == nullptr)
            break;

        ipi_request_t* request = CONTAINER_OF((atomic_stack_node_t*) node, ipi_request_t, node);
        pmm_free_page(PTM_FROM_HHDM(request));
    }

    sched_preempt_enable();
}
