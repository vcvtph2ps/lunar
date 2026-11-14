#include <arch/x86_64/cpu_local.h>
#include <arch/x86_64/hardware/lapic.h>
#include <arch/x86_64/interrupts/interrupt.h>
#include <arch/x86_64/interrupts/interrupt_alloc.h>
#include <common/arch.h>
#include <common/init.h>
#include <common/interrupts/dpc.h>
#include <common/interrupts/interrupt.h>
#include <common/interrupts/ipi.h>
#include <common/log.h>
#include <common/sync/spinlock.h>
#include <lib/helpers.h>
#include <lib/list.h>
#include <memory/pmm.h>
#include <memory/ptm.h>

uint8_t g_ipi_vector = 0xff;
uint8_t g_panic_ipi_vector = 0xff;

static void ipi_handle_deferred(void* ctx) {
    (void) ctx;
    ipi_reclaim();
}

static void ipi_interrupt_handler(arch_interrupt_frame_t* frame, void* ctx) {
    (void) ctx;
    (void) frame;
    ipi_handle();
    dpc_queue(CPU_LOCAL_GET_SELF()->ipi.dpc_free_item);
}

[[noreturn]] static void ipi_panic_interrupt_handler(arch_interrupt_frame_t* frame, void* ctx) {
    (void) ctx;
    (void) frame;

    // @note the interrupt hnadler cannot EOI for us
    arch_lapic_eoi();
    ipi_handle_panic();
}

void ipi_init(uint32_t core_id) {
    if(INIT_CORE_IS_BSP(core_id)) {
        g_ipi_vector = arch_interrupt_alloc_allocate();
        g_panic_ipi_vector = arch_interrupt_alloc_allocate();
        interrupt_set_hardirq_handler(g_ipi_vector, ipi_interrupt_handler, nullptr);
        interrupt_set_hardirq_handler(g_panic_ipi_vector, ipi_panic_interrupt_handler, nullptr);
    }
    CPU_LOCAL_GET_SELF()->ipi.dpc_free_item = dpc_create(ipi_handle_deferred, nullptr);
}

void arch_send_ipi(uint32_t core_id) {
    if(g_ipi_vector == 0xff) {
        LOG_WARN("IPI vector not allocated yet, cannot send IPI to core %d\n", core_id);
        return;
    }
    arch_lapic_send_ipi(arch_cpu_local_get_core_lapic_id(core_id), g_ipi_vector);
}

void arch_send_panic_ipi(uint32_t core_id) {
    if(g_panic_ipi_vector == 0xff) {
        return;
    }
    arch_lapic_send_ipi(arch_cpu_local_get_core_lapic_id(core_id), g_panic_ipi_vector);
}
