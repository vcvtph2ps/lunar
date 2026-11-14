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

#include "lib/types.h"

static void ipi_completion_init(ipi_completion_t* completion) {
    ATOMIC_STORE(&completion->remaining, 0, ATOMIC_RELAXED);
}

static void ipi_completion_add(ipi_completion_t* completion) {
    ATOMIC_LOAD_ADD(&completion->remaining, 1, ATOMIC_RELEASE);
}

static void ipi_completion_signal(ipi_completion_t* completion) {
    ATOMIC_LOAD_SUB(&completion->remaining, 1, ATOMIC_ACQ_REL);
}

static void ipi_completion_wait(ipi_completion_t* completion) {
    while(ATOMIC_LOAD(&completion->remaining, ATOMIC_ACQUIRE) != 0) {
        arch_spin_hint();
    }
}

static ipi_request_t* ipi_request_create(ipi_message_t message, ipi_completion_t* completion) {
    // @todo: figure out something better than just using a whole physical page here....
    ipi_request_t* request = (ipi_request_t*) PTM_TO_HHDM(pmm_alloc_page(PMM_FLAG_PANIC));
    request->completion = completion;
    memory_copy(&request->message, &message, sizeof(ipi_message_t));
    return request;
}

static void ipi_send_internal(uint32_t core_id, ipi_message_t message, ipi_completion_t* completion) {
    arch_cpu_local_t* cpu_local = cpu_local_get_other(core_id);
    if(!cpu_local->online) {
        return;
    }

    // A core that has not handed off to the scheduler still runs with interrupts disabled and may be spinning on a lock the sender holds
    ipi_completion_t* request_completion = cpu_local->scheduler.threaded ? completion : nullptr;

    ipi_request_t* request = ipi_request_create(message, request_completion);
    if(request_completion != nullptr) {
        // Bump the count before the request becomes visible to the target.
        ipi_completion_add(request_completion);
    }

    atomic_stack_push(&cpu_local->ipi.queue, &request->node);
    arch_send_ipi(core_id);
}

void ipi_send(uint32_t core_id, ipi_message_t message) {
    sched_preempt_disable();

    ipi_completion_t completion;
    ipi_completion_init(&completion);

    ipi_send_internal(core_id, message, &completion);
    ipi_completion_wait(&completion);

    sched_preempt_enable();
}

void ipi_broadcast(ipi_message_t message) {
    sched_preempt_disable();

    ipi_completion_t completion;
    ipi_completion_init(&completion);

    uint64_t self = CPU_LOCAL_READ(core_id);
    for(uint32_t i = 0; i < g_init_boot_info->core_count; i++) {
        if(i == self)
            continue;
        ipi_send_internal(i, message, &completion);
    }

    ipi_completion_wait(&completion);

    sched_preempt_enable();
}

void ipi_send_panic(uint32_t core_id) {
    arch_send_panic_ipi(core_id);
}

void ipi_broadcast_panic(void) {
    if(g_init_boot_info == nullptr) {
        return;
    }

    uint64_t self = CPU_LOCAL_READ(core_id);
    for(uint32_t i = 0; i < g_init_boot_info->core_count; i++) {
        if(i == self)
            continue;
        ipi_send_panic(i);
    }
}

static void process_ipi(ipi_message_t message) {
    switch(message.type) {
        case IPI_TLB_FLUSH:     ptm_flush_tlb(message.tlb_flush.vaddr, message.tlb_flush.length); break;
        case IPI_TLB_FLUSH_ALL: ptm_flush_tlb_all(); break;
        default:                arch_panic("Received unknown IPI type %d on core %d\n", message.type, CPU_LOCAL_READ(core_id));
    }
}

[[noreturn]] void ipi_handle_panic(void) {
    arch_shutdown_cpu();
}

#define IPI_TLB_FLUSH_ALL_THRESHOLD 32

#define IPI_IS_TLB_FLUSH(type) ((type) == IPI_TLB_FLUSH || (type) == IPI_TLB_FLUSH_ALL)

static bool should_flush_all(stack_t* stack) {
    size_t pages = 0;

    STACK_FOR_EACH(stack, node) {
        ipi_request_t* request = CONTAINER_OF((atomic_stack_node_t*) node, ipi_request_t, node);

        switch(request->message.type) {
            case IPI_TLB_FLUSH_ALL: return true;
            case IPI_TLB_FLUSH:
                pages += ALIGN_UP(request->message.tlb_flush.length, PAGE_SIZE_DEFAULT) / PAGE_SIZE_DEFAULT;
                if(pages >= IPI_TLB_FLUSH_ALL_THRESHOLD)
                    return true;
                break;
            default: break;
        }
    }

    return false;
}

void ipi_handle() {
    sched_preempt_disable();

    stack_t stack = atomic_stack_take_all(&CPU_LOCAL_GET_SELF()->ipi.queue);

    bool ignore_tlb_flushes = should_flush_all(&stack);
    if(ignore_tlb_flushes)
        ptm_flush_tlb_all();

    stack_node_t* node;
    while((node = stack_pop(&stack)) != nullptr) {
        ipi_request_t* request = CONTAINER_OF((atomic_stack_node_t*) node, ipi_request_t, node);

        // We've already flushed the whole tlb, we can ignore all tlb flush requests
        if(!(ignore_tlb_flushes && IPI_IS_TLB_FLUSH(request->message.type)))
            process_ipi(request->message);

        // Acknowledge after the flush so the sender knows we are done
        if(request->completion != nullptr)
            ipi_completion_signal(request->completion);

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
