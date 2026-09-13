#include <arch/x86_64/cpu_local.h>
#include <arch/x86_64/hardware/lapic.h>
#include <arch/x86_64/interrupts/interrupt.h>
#include <arch/x86_64/interrupts/interrupt_alloc.h>
#include <common/arch.h>
#include <common/init.h>
#include <common/interrupts/dw.h>
#include <common/interrupts/interrupt.h>
#include <common/interrupts/ipi.h>
#include <common/log.h>
#include <common/sync/spinlock.h>
#include <lib/helpers.h>
#include <lib/list.h>
#include <memory/heap.h>

uint8_t g_ipi_vector = 0xff;

static void ipi_handle_deferred(void* ctx) {
    (void) ctx;
    while(true) {
        arch_interrupt_state_t state = spinlock_noint_lock(&CPU_LOCAL_GET_SELF()->ipi.lock);
        list_node_t* node = list_pop(&CPU_LOCAL_GET_SELF()->ipi.pending_free_list);
        spinlock_noint_unlock(&CPU_LOCAL_GET_SELF()->ipi.lock, state);

        if(node == nullptr) {
            break;
        }

        ipi_request_t* request = CONTAINER_OF(node, ipi_request_t, pending_free_node);
        heap_free(request, sizeof(ipi_request_t));
    }
}

static void ipi_interrupt_handler(arch_interrupt_frame_t* frame, void* ctx) {
    (void) ctx;
    (void) frame;
    ipi_message_t message;
    while(ipi_pop(&message)) {
        ipi_handle(message);
    }
    dw_queue(CPU_LOCAL_GET_SELF()->ipi.dw_free_item);
}

void ipi_init(uint32_t core_id) {
    if(INIT_CORE_IS_BSP(core_id)) {
        g_ipi_vector = arch_interrupt_alloc_allocate();
        interrupt_set_hardirq_handler(g_ipi_vector, ipi_interrupt_handler, nullptr);
    }
    CPU_LOCAL_GET_SELF()->ipi.dw_free_item = dw_create(ipi_handle_deferred, nullptr);
    CPU_LOCAL_GET_SELF()->ipi.dw_free_item->cleanup_fn = nullptr;
}

void arch_send_ipi(uint32_t core_id) {
    if(g_ipi_vector == 0xff) {
        LOG_WARN("IPI vector not allocated yet, cannot send IPI to core %d\n", core_id);
        return;
    }
    arch_lapic_send_ipi(arch_cpu_local_get_core_lapic_id(core_id), g_ipi_vector);
}
