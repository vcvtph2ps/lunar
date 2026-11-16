// Heavily inspired and refactored from
// https://git.evalyngoemer.com/evalynOS/evalynOS/src/commit/ee92dac22b5567f597cce3c36dba44af0b87222b/kernel/src/arch/x86_64/drivers/uart16550.c

#include <arch/x86_64/hardware/ioapic.h>
#include <arch/x86_64/interrupts/interrupt_alloc.h>
#include <common/arch.h>
#include <common/hardware/uart16550.h>
#include <common/init.h>
#include <common/interrupts/interrupt.h>
#include <common/log.h>
#include <lib/helpers.h>
#include <lib/types.h>
#include <memory/vm.h>
#include <stdbool.h>
#include <stdint.h>
#include <uacpi/resources.h>
#include <uacpi/status.h>
#include <uacpi/types.h>
#include <uacpi/utilities.h>

static uart16550_t g_early_uart;

typedef struct {
    bool is_mmio;
    uint64_t base;
    uint64_t size;
    /// 0xFF if not present
    uint8_t irq;
    bool irq_low_polarity;
    bool irq_edge_triggered;
} uart_crs_t;

static bool uart_console_from_bootinfo(bool mmio) {
    uint64_t base = g_init_boot_info->serial.base;
    uint64_t size = g_init_boot_info->serial.size;

    io_region_t io;
    if(mmio) {
        if(size == 0) {
            size = PAGE_SIZE_DEFAULT;
        }
        virt_addr_t virt = (virt_addr_t) vm_map_direct(g_vm_global_address_space, VM_NO_HINT, ALIGN_UP(size, PAGE_SIZE_DEFAULT), VM_PROT_RW, VM_CACHE_DISABLE, base, VM_FLAG_NO_PAGEDB);
        if(virt == 0) {
            LOG_FAIL("uart16550: failed to map serial MMIO region at 0x%lx\n", (unsigned long) base);
            return false;
        }
        io = IO_REGION_MMIO(virt);
    } else {
        io = IO_REGION_PORT(base);
    }

    g_early_uart = (uart16550_t) {
        .io = io,
        .irq = UART16550_IRQ_NONE,
    };

    if(!uart16550_init(&g_early_uart, UART16550_DIVISOR_115200)) {
        LOG_WARN("uart16550: failed to initialize the prekernel serial device\n");
        return false;
    }

    uart16550_set_default(&g_early_uart);
    return true;
}

void arch_uart16550_early_setup() {
    switch(g_init_boot_info->serial.type) {
        case BOOTINFO_SERIAL_TYPE_PIO:
            if(!uart_console_from_bootinfo(false)) {
                return;
            }
            break;
        case BOOTINFO_SERIAL_TYPE_MMIO: LOG_INFO("uart16550: MMIO serial console deferred until the VM is available\n"); return;
        default:                        LOG_WARN("uart16550: prekernel did not provide a serial device\n"); return;
    }

    LOG_OKAY("Serial init\n");

    if(!uart16550_add_log_sink()) {
        LOG_OKAY("Failed to add log sink; serial output will not work :(\n");
    }

    uart16550_send(g_uart16550_default, '\n');
    g_uart16550_works = true;
}

static const char* g_uart_hids[] = { "PNP0501", nullptr };

static uacpi_iteration_decision uart_parse_resource(void* user, uacpi_resource* resource) {
    uart_crs_t* crs = (uart_crs_t*) user;

    switch(resource->type) {
        case UACPI_RESOURCE_TYPE_IO: {
            uacpi_resource_io* r = &resource->io;
            if(crs->base == 0) {
                crs->base = r->minimum;
                crs->is_mmio = false;
                crs->size = r->length;
            }
            break;
        };
        case UACPI_RESOURCE_TYPE_FIXED_IO: {
            uacpi_resource_fixed_io* r = &resource->fixed_io;
            if(crs->base == 0) {
                crs->base = r->address;
                crs->is_mmio = false;
                crs->size = r->length;
            }
            break;
        }
        case UACPI_RESOURCE_TYPE_MEMORY32: {
            uacpi_resource_memory32* r = &resource->memory32;
            if(crs->base == 0) {
                crs->base = r->minimum;
                crs->is_mmio = true;
                crs->size = r->length;
            }
            break;
        }
        case UACPI_RESOURCE_TYPE_FIXED_MEMORY32: {
            uacpi_resource_fixed_memory32* r = &resource->fixed_memory32;
            if(crs->base == 0) {
                crs->base = r->address;
                crs->is_mmio = true;
                crs->size = r->length;
            }
            break;
        }
        case UACPI_RESOURCE_TYPE_ADDRESS32: {
            uacpi_resource_address32* r = &resource->address32;
            if(crs->base == 0) {
                crs->base = r->minimum;
                crs->is_mmio = r->common.type == UACPI_RANGE_MEMORY;
                crs->size = r->address_length;
            }
            break;
        }
        case UACPI_RESOURCE_TYPE_ADDRESS64: {
            uacpi_resource_address64* r = &resource->address64;
            if(crs->base == 0) {
                crs->base = r->minimum;
                crs->is_mmio = r->common.type == UACPI_RANGE_MEMORY;
                crs->size = r->address_length;
            }
            break;
        }
        case UACPI_RESOURCE_TYPE_IRQ: {
            uacpi_resource_irq* r = &resource->irq;
            if(crs->irq == 0xff && r->num_irqs > 0) {
                crs->irq = (uint8_t) r->irqs[0];
                crs->irq_low_polarity = (r->polarity == UACPI_POLARITY_ACTIVE_LOW);
                crs->irq_edge_triggered = (r->triggering == UACPI_TRIGGERING_EDGE);
            }
            break;
        }
        case UACPI_RESOURCE_TYPE_EXTENDED_IRQ: {
            uacpi_resource_extended_irq* r = &resource->extended_irq;
            if(crs->irq == 0xff && r->num_irqs > 0) {
                crs->irq = (uint8_t) r->irqs[0];
                crs->irq_low_polarity = (r->polarity == UACPI_POLARITY_ACTIVE_LOW);
                crs->irq_edge_triggered = (r->triggering == UACPI_TRIGGERING_EDGE);
            }
            break;
        }
        case UACPI_RESOURCE_TYPE_END_TAG: break;
        default:                          LOG_WARN("ACPI: Unknown UART resource type %u\n", resource->type); break;
    }

    return UACPI_ITERATION_DECISION_CONTINUE;
}

typedef struct {
    uart_crs_t console;
    uart_crs_t result;
    bool found;
} uart_find_ctx_t;

static uacpi_iteration_decision uart_device_find_callback(void* user, uacpi_namespace_node* node, uacpi_u32 depth) {
    (void) depth;
    uart_find_ctx_t* ctx = (uart_find_ctx_t*) user;

    uart_crs_t crs = { .irq = 0xFF };
    if(uacpi_unlikely_error(uacpi_for_each_device_resource(node, "_CRS", uart_parse_resource, &crs))) {
        return UACPI_ITERATION_DECISION_CONTINUE;
    }

    if(crs.base != ctx->console.base || crs.is_mmio != ctx->console.is_mmio || crs.irq == 0xFF) {
        return UACPI_ITERATION_DECISION_CONTINUE;
    }

    ctx->result = crs;
    ctx->found = true;
    return UACPI_ITERATION_DECISION_BREAK;
}

[[nodiscard]] static bool uart_crs_for_console(const uart_crs_t* console, uart_crs_t* out) {
    uart_find_ctx_t ctx = {
        .console = *console,
        .result = { .irq = 0xFF },
        .found = false,
    };

    uacpi_find_devices_at(uacpi_namespace_root(), g_uart_hids, uart_device_find_callback, &ctx);

    *out = ctx.result;
    return ctx.found;
}

static void uart_interrupt_handler(arch_interrupt_frame_t* frame, void* ctx) {
    (void) frame;
    uart16550_handle_interrupt((uart16550_t*) ctx);
}

void arch_uart16550_setup() {
    if(g_uart16550_default == nullptr) {
        if(g_init_boot_info->serial.type != BOOTINFO_SERIAL_TYPE_MMIO || !uart_console_from_bootinfo(true)) {
            LOG_WARN("uart16550: no console UART to wire an interrupt for\n");
            return;
        }

        if(!uart16550_add_log_sink()) {
            LOG_OKAY("Failed to add log sink; serial output will not work :(\n");
        }

        uart16550_send(g_uart16550_default, '\n');
        g_uart16550_works = true;
        LOG_OKAY("Serial init\n");
    }

    uart_crs_t console = {
        .is_mmio = g_init_boot_info->serial.type == BOOTINFO_SERIAL_TYPE_MMIO,
        .base = g_init_boot_info->serial.base,
        .size = g_init_boot_info->serial.size,
    };

    uart_crs_t crs;
    if(!uart_crs_for_console(&console, &crs)) {
        LOG_WARN("uart16550: no interrupt found for the console UART, receive will be unavailable\n");
        return;
    }

    uint8_t vector = arch_interrupt_alloc_allocate();
    if(vector == 0) {
        arch_panic("Failed to allocate interrupt vector for uart16550\n");
    }

    uart16550_t* uart = g_uart16550_default;
    uart->irq = crs.irq;
    uart->irq_low_polarity = crs.irq_low_polarity;
    uart->irq_edge_triggered = crs.irq_edge_triggered;

    interrupt_set_hardirq_handler(vector, uart_interrupt_handler, uart);

    if(!uart16550_start_rx_worker()) {
        LOG_WARN("uart16550: failed to start receive worker\n");
    }

    // @todo: lapic allocation
    arch_ioapic_map_legacy_irq(crs.irq, 0, crs.irq_low_polarity, crs.irq_edge_triggered, vector);
    uart16550_set_rx_interrupt(uart, true);
}
