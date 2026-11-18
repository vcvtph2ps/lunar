#include <arch/riscv64/dt.h>
#include <arch/riscv64/hardware/plic.h>
#include <common/arch.h>
#include <common/hardware/uart16550.h>
#include <common/init.h>
#include <common/interrupts/interrupt.h>
#include <common/log.h>
#include <lib/types.h>
#include <memory/vm.h>
#include <stdbool.h>
#include <stdint.h>
#include <uacpi/acpi.h>
#include <uacpi/status.h>
#include <uacpi/tables.h>
#include <uacpi/uacpi.h>

static const char* g_uart_compatibles[] = { "ns16550a", "ns16550", nullptr };

static void uart_interrupt_handler(arch_interrupt_frame_t* frame, void* ctx) {
    (void) frame;
    uart16550_handle_interrupt((uart16550_t*) ctx);
}

/// @brief Finds the interrupt of the serial node whose reg base matches base in the device tree
[[nodiscard]] static uint32_t uart16550_find_irq(void) {
    const void* fdt = arch_dt_blob();
    if(fdt == nullptr) {
        return UART16550_IRQ_NONE;
    }

    uint64_t base = g_init_boot_info->serial.base;

    int node = -1;
    while((node = arch_dt_node_offset_by_compatible_list(fdt, node, g_uart_compatibles)) >= 0) {
        uint64_t address = 0;
        if(!arch_dt_get_reg(fdt, node, &address, nullptr)) {
            continue;
        }
        if(address != base) {
            continue;
        }

        uint32_t interrupt = 0;
        if(arch_dt_get_interrupt(fdt, node, &interrupt)) {
            return interrupt;
        }
    }

    return UART16550_IRQ_NONE;
}

[[nodiscard]] static uint32_t uart16550_find_irq_acpi(void) {
    if(!g_init_boot_info->rdsp_physical || uacpi_get_current_init_level() < UACPI_INIT_LEVEL_SUBSYSTEM_INITIALIZED) {
        return UART16550_IRQ_NONE;
    }

    uacpi_table spcr;
    if(uacpi_table_find_by_signature(ACPI_SPCR_SIGNATURE, &spcr) != UACPI_STATUS_OK) {
        return UART16550_IRQ_NONE;
    }

    const struct acpi_spcr* table = (const struct acpi_spcr*) spcr.ptr;
    uint32_t irq = UART16550_IRQ_NONE;
    if(table->interrupt_type == ACPI_SPCR_INTERRUPT_TYPE_PLIC_APLIC && table->gsi != 0) {
        uint32_t plic_irq = arch_plic_gsi_to_irq(table->gsi);
        if(plic_irq != 0) {
            irq = plic_irq;
        }
    }

    uacpi_table_unref(&spcr);
    return irq;
}

void uart16550_setup(void) {
    if(g_init_boot_info->serial.type != BOOTINFO_SERIAL_TYPE_MMIO) {
        LOG_WARN("uart16550: prekernel did not provide a memory-mapped serial device\n");
        return;
    }

    uint64_t address = g_init_boot_info->serial.base;
    uint64_t size = g_init_boot_info->serial.size != 0 ? g_init_boot_info->serial.size : PAGE_SIZE_DEFAULT;

    virt_addr_t virt = (virt_addr_t) vm_map_direct(g_vm_global_address_space, VM_NO_HINT, ALIGN_UP(size, PAGE_SIZE_DEFAULT), VM_PROT_RW, VM_CACHE_DISABLE, address, VM_FLAG_NO_PAGEDB);
    if(virt == 0) {
        arch_panic("uart16550: failed to map MMIO region at 0x%lx\n", (unsigned long) address);
    }

    io_region_t io = IO_REGION_MMIO(virt);
    uart16550_t* uart = uart16550_create(&io, UART16550_IRQ_NONE, false, false);
    if(uart == nullptr) {
        LOG_FAIL("uart16550: failed to allocate UART for 0x%lx\n", (unsigned long) address);
        return;
    }

    if(!uart16550_init(uart, UART16550_DIVISOR_115200)) {
        LOG_FAIL("uart16550: UART at 0x%lx failed to initialize\n", (unsigned long) address);
        return;
    }

    uart16550_register(uart);
    uart16550_set_default(uart);
    LOG_OKAY("uart16550: console UART at 0x%lx (mmio 0x%lx)\n", (unsigned long) address, (unsigned long) virt);

    uint32_t irq = uart16550_find_irq();
    if(irq == UART16550_IRQ_NONE) {
        irq = uart16550_find_irq_acpi();
    }
    if(irq == UART16550_IRQ_NONE) {
        LOG_WARN("uart16550: no interrupt found for the console UART, receive will be unavailable\n");
        return;
    }

    if(!arch_plic_is_initialized()) {
        LOG_WARN("uart16550: PLIC is not initialized, receive will be unavailable\n");
        return;
    }

    uart->irq = irq;
    arch_plic_set_handler(irq, uart_interrupt_handler, uart);
    arch_plic_enable_irq(irq, 1);

    if(!uart16550_start_rx_worker()) {
        LOG_WARN("uart16550: failed to start receive worker\n");
    }

    uart16550_set_rx_interrupt(uart, true);
    LOG_OKAY("uart16550: receive enabled on irq %u\n", irq);
}
