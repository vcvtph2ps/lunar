#include <arch/cpu_local.h>
#include <arch/hardware/lapic.h>
#include <arch/interrupts/interrupt.h>
#include <arch/interrupts/interrupt_alloc.h>
#include <common/arch.h>
#include <common/init.h>
#include <common/interrupts/interrupt.h>
#include <common/interrupts/ipi.h>
#include <common/log.h>

uint8_t g_ipi_vector = 0xff;

static void ipi_interrupt_handler(arch_interrupt_frame_t* frame) {
    (void) frame;
    ipi_message_t message;
    while(ipi_pop(&message)) { ipi_handle(message); }
}

void ipi_init(uint32_t core_id) {
    if(INIT_CORE_IS_BSP(core_id)) {
        g_ipi_vector = arch_interrupt_alloc_allocate();
        interrupt_set_handler(g_ipi_vector, ipi_interrupt_handler);
        return;
    }
}

void arch_send_ipi(uint32_t core_id) {
    if(g_ipi_vector == 0xff) { LOG_WARN("IPI vector not allocated yet, cannot send IPI to core %d\n", core_id); }
    arch_lapic_send_ipi(arch_cpu_local_get_core_lapic_id(core_id), g_ipi_vector);
}
