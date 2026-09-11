// Heavily inspired and refactored from
// https://git.evalyngoemer.com/evalynOS/evalynOS/src/commit/ee92dac22b5567f597cce3c36dba44af0b87222b/kernel/src/arch/x86_64/drivers/16550uart.c

#include <arch/x86_64/hardware/16550uart.h>
#include <arch/x86_64/hardware/ioapic.h>
#include <arch/x86_64/interrupts/interrupt.h>
#include <arch/x86_64/interrupts/interrupt_alloc.h>
#include <arch/x86_64/io.h>
#include <common/interrupts/dw.h>
#include <common/interrupts/interrupt.h>
#include <common/log.h>
#include <lib/helpers.h>
#include <lib/list.h>
#include <lib/string.h>
#include <memory/heap.h>
#include <stdbool.h>
#include <stdint.h>
#include <uacpi/resources.h>
#include <uacpi/status.h>
#include <uacpi/types.h>
#include <uacpi/utilities.h>

/* Serial Port Registers */
#define SERIAL_RX_BUFF 0 // read  ; DLAB = 0
#define SERIAL_TX_BUFF 0 // write ; DLAB = 0
#define SERIAL_INTR_CONF 1 // both  ; DLAB = 0
#define SERIAL_DLAB_DIV_LO 0 // both  ; DLAB = 1
#define SERIAL_DLAB_DIV_HI 1 // both  ; DLAB = 1
#define SERIAL_INTR_INFO 2 // read
#define SERIAL_FIFO_CONF 2 // write
#define SERIAL_LINE_CONF 3 // both
#define SERIAL_MODEM_CONF 4 // both
#define SERIAL_LINE_INFO 5 // read
#define SERIAL_MODEM_INFO 6 // read
#define SERIAL_SCRATCH_REG 7 // both

/* FIFO Config */
#define SERIAL_FIFO_THRESH_1B 0x00 // bit 6 & 7 unset
#define SERIAL_FIFO_THRESH_4B 0x40 // bit 6 set
#define SERIAL_FIFO_THRESH_8B 0x80 // bit 7 set
#define SERIAL_FIFO_THRESH_14B 0xC0 // bit 6 & 7 set
#define SERIAL_FIFO_ENABLE 0x01 // bit 0 set
#define SERIAL_FIFO_RX_FLUSH 0x02 // bit 1 set
#define SERIAL_FIFO_TX_FLUSH 0x04 // bit 2 set

/* Line Control Register */
#define SERIAL_LCR_8BIT 0x03 // bit 0 & 1 set
#define SERIAL_LCR_7BIT 0x01 // bit 0 set
#define SERIAL_LCR_6BIT 0x02 // bit 1 set
#define SERIAL_LCR_5BIT 0x00 // bit 0 & 1 unset
#define SERIAL_LCR_1STOP 0x00 // bit 2 unset
#define SERIAL_LCR_2STOP 0x04 // bit 2 set
#define SERIAL_LCR_PARITY_NONE 0x00 // bit 3 & 4 & 5 unset
#define SERIAL_LCR_PARITY_ODD 0x08 // bit 3 set
#define SERIAL_LCR_PARITY_EVEN 0x18 // bit 3 & 4 set
#define SERIAL_LCR_PARITY_MARK 0x28 // bit 5 & 3 set
#define SERIAL_LCR_PARITY_SPCE 0x38 // bit 3 & 4 & 5 set

/* Modem Control Register*/
#define SERIAL_MCR_TX_ENABLE 0x01 // bit 0 set (DTR)
#define SERIAL_MCR_RX_ENABLE 0x02 // bit 1 set (RTS)
#define SERIAL_MCR_IRQ_ENABLE 0x08 // bit 3 set
#define SERIAL_MCR_LOOP_ENABLE 0x10 // bit 4 set

/* Baud Rate Divisors */
#define SERIAL_115200_BAUD 1
#define SERIAL_57600_BAUD 2
#define SERIAL_38400_BAUD 3
#define SERIAL_19200_BAUD 6
#define SERIAL_9600_BAUD 12
#define SERIAL_4800_BAUD 24
#define SERIAL_2400_BAUD 48
#define SERIAL_1200_BAUD 96
#define SERIAL_600_BAUD 192
#define SERIAL_300_BAUD 384

/* Misc */
#define SERIAL_DLAB_BIT 0x80
#define SERIAL_DATA_READY_BIT 0x01
#define SERIAL_TX_EMPTY_BIT 0x20
#define SERIAL_TEST_MAGIC 0x69
#define SERIAL_TEST_RETRIES 5

// TODO; use uACPI to find valid serial port
// just assume one at the default port for debugging
// also always assume it exists under a hypervisor
// for early logging in VMs even with ACPI
bool g_default_uart_exists = false;
static arch_16550uart_t g_early_uart = {};

arch_16550uart_t* g_arch_16550uart_default_uart = nullptr;

list_t g_uart_list = LIST_INIT;

static inline void serial_set_dlab(uint16_t port, bool setting) {
    arch_io_wait();
    uint8_t lcr = arch_io_port_read_u8(port + SERIAL_LINE_CONF);
    arch_io_wait();
    if(setting) {
        arch_io_wait();
        arch_io_port_write_u8(port + SERIAL_LINE_CONF, lcr | SERIAL_DLAB_BIT);
        arch_io_wait();
    } else {
        arch_io_wait();
        arch_io_port_write_u8(port + SERIAL_LINE_CONF, lcr & ~SERIAL_DLAB_BIT);
        arch_io_wait();
    }
}

static inline void serial_set_divisor(uint16_t port, uint16_t divsor) {
    serial_set_dlab(port, true);
    arch_io_wait();
    arch_io_port_write_u8(port + SERIAL_DLAB_DIV_LO, divsor & 0xff);
    arch_io_wait();
    arch_io_wait();
    arch_io_port_write_u8(port + SERIAL_DLAB_DIV_HI, (divsor >> 8) & 0xff);
    arch_io_wait();
    serial_set_dlab(port, false);
}

static inline void serial_set_interrupts(uint16_t port, uint8_t setting) {
    serial_set_dlab(port, false);
    arch_io_wait();
    arch_io_port_write_u8(port + SERIAL_INTR_CONF, setting);
    arch_io_wait();
}

static inline void serial_set_mcr(uint16_t port, uint8_t setting) {
    arch_io_wait();
    arch_io_port_write_u8(port + SERIAL_MODEM_CONF, setting);
    arch_io_wait();
}

static inline void serial_set_lcr(uint16_t port, uint8_t lcr) {
    arch_io_wait();
    arch_io_port_write_u8(port + SERIAL_LINE_CONF, lcr);
    arch_io_wait();
}

static inline void serial_set_fifo(uint16_t port, uint8_t fifo) {
    arch_io_wait();
    arch_io_port_write_u8(port + SERIAL_FIFO_CONF, fifo);
    arch_io_wait();
}

/// @warning: clobbers serial port config
/// @returns: 2 on success; 1 on partial failure; 0 on total failure
static int serial_test(uint16_t port) {
    serial_set_divisor(port, SERIAL_115200_BAUD);
    serial_set_lcr(port, SERIAL_LCR_8BIT | SERIAL_LCR_1STOP | SERIAL_LCR_PARITY_NONE);
    serial_set_fifo(port, SERIAL_FIFO_TX_FLUSH | SERIAL_FIFO_RX_FLUSH);
    serial_set_mcr(port, SERIAL_MCR_TX_ENABLE | SERIAL_MCR_RX_ENABLE | SERIAL_MCR_LOOP_ENABLE);
    serial_set_dlab(port, false);
    for(int i = 0; i < SERIAL_TEST_RETRIES; i++) {
        arch_io_port_write_u8(port + SERIAL_TX_BUFF, SERIAL_TEST_MAGIC);
        for(int i = 0; i < 256; i++) arch_io_wait();

        if(arch_io_port_read_u8(port + SERIAL_RX_BUFF) == SERIAL_TEST_MAGIC) return 2;
    }

    // fallback test to just make sure it exists at all
    arch_io_port_write_u8(port + SERIAL_SCRATCH_REG, SERIAL_TEST_MAGIC);
    if(arch_io_port_read_u8(port + SERIAL_SCRATCH_REG) == SERIAL_TEST_MAGIC) return 1;
    return 0;
}

int arch_16550uart_transmit_empty(arch_16550uart_t* uart) {
    arch_io_wait();
    uint8_t data = arch_io_port_read_u8(uart->uart_port + SERIAL_LINE_INFO) & SERIAL_TX_EMPTY_BIT;
    arch_io_wait();
    return data;
}

int arch_16550uart_data_ready(arch_16550uart_t* uart) {
    arch_io_wait();
    uint8_t data = arch_io_port_read_u8(uart->uart_port + SERIAL_LINE_INFO) & SERIAL_DATA_READY_BIT;
    arch_io_wait();
    return data;
}

void arch_16550uart_send(arch_16550uart_t* uart, char c) {
    for(int i = 0; i < 100000; i++) {
        if(arch_16550uart_transmit_empty(uart)) break;
        arch_spin_hint();
    }
    arch_io_wait();
    arch_io_port_write_u8(uart->uart_port + SERIAL_TX_BUFF, c);
    arch_io_wait();
}

int arch_16550uart_read(arch_16550uart_t* uart) {
    if(!arch_16550uart_data_ready(uart)) return -1;

    arch_io_wait();
    uint8_t data = arch_io_port_read_u8(uart->uart_port + SERIAL_RX_BUFF);
    arch_io_wait();
    return data;
}

static void serial_sink(int c, void* ctx) {
    (void) ctx;
    arch_16550uart_send(g_arch_16550uart_default_uart, (char) c);
}

static void serial_rx_dw_handler(void* ctx) {
    arch_16550uart_t* uart = (arch_16550uart_t*) ctx;
    while(1) {
        int c = arch_16550uart_read(uart);
        if(c < 0) break;
        // LOG_DBGL("serial: %c (%d)\n", c, c);
        uart->on_recv(uart->recv_ctx, c);
    }
}

static void serial_rx_handler(arch_interrupt_frame_t* frame, void* ctx) {
    (void) frame;

    arch_16550uart_t* uart = (arch_16550uart_t*) ctx;
    dw_queue(uart->dw_item);
}

static bool try_port(uint16_t port) {
    int status = serial_test(port);
    if(status == 0) {
        return false;
    }

    serial_set_interrupts(port, false);
    serial_set_divisor(port, SERIAL_115200_BAUD);
    serial_set_lcr(port, SERIAL_LCR_8BIT | SERIAL_LCR_1STOP | SERIAL_LCR_PARITY_NONE);
    serial_set_fifo(port, SERIAL_FIFO_ENABLE | SERIAL_FIFO_THRESH_1B | SERIAL_FIFO_TX_FLUSH | SERIAL_FIFO_RX_FLUSH);
    serial_set_mcr(port, SERIAL_MCR_TX_ENABLE | SERIAL_MCR_RX_ENABLE | SERIAL_MCR_IRQ_ENABLE);
    serial_set_dlab(port, false);

    if(status == 1) {
        LOG_FAIL("Serial port at I/O port 0x%x failed part of self test\n", port);
        return false;
    }

    return true;
}

void arch_16550uart_early_setup() {
    uint16_t default_ports[] = { 0x3f8, 0x2f8, 0x3e8, 0x2e8 };
    uint16_t serial_port = 0;
    for(size_t i = 0; i < sizeof(default_ports) / sizeof(uint16_t); i++) {
        if(try_port(default_ports[i])) {
            serial_port = default_ports[i];
            break;
        }
    }

    if(serial_port == 0) {
        LOG_WARN("16550uart Failed to early init; Do you lack a serial port at I/O port [0x3f8, 0x2f8, 0x3e8, 0x2e8]?\n");
        LOG_WARN("Serial may be need to be discovered via ACPI\n");
        return;
    }

    g_arch_16550uart_default_uart = &g_early_uart;
    g_arch_16550uart_default_uart->uart_port = serial_port;
    g_default_uart_exists = true;

    LOG_OKAY("Serial init\n");

    log_sink_t sink = {
        .min_level = LOG_LEVEL_STRC,
        .write = serial_sink,
        .ctx = nullptr,
    };

    if(!log_add_sink(&sink)) {
        LOG_OKAY("Failed to add log sink; serial output will not work :(\n");
    }

    serial_sink('\n', &g_arch_16550uart_default_uart);
}


static const char* g_uart_hids[] = { "PNP0501", nullptr };

typedef struct {
    uint16_t io_port;
    /// 0xFF if not present
    uint8_t irq;
    bool irq_low_polarity;
    bool irq_edge_triggered;
} uart_crs_t;

// @todo: this shit is wrong
static uacpi_iteration_decision uart_parse_resource(void* user, uacpi_resource* resource) {
    uart_crs_t* crs = (uart_crs_t*) user;

    switch(resource->type) {
        case UACPI_RESOURCE_TYPE_IO: {
            uacpi_resource_io* r = &resource->io;
            if(crs->io_port == 0) {
                crs->io_port = r->minimum;
            }
            break;
        };
        case UACPI_RESOURCE_TYPE_FIXED_IO: {
            uacpi_resource_fixed_io* r = &resource->fixed_io;
            if(crs->io_port == 0) {
                crs->io_port = r->address;
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

static uacpi_iteration_decision uart_device_find_callback(void* user, uacpi_namespace_node* node, uacpi_u32 depth) {
    (void) user;
    (void) depth;

    uart_crs_t crs = { .io_port = 0, .irq = 0xFF };
    uacpi_status status = uacpi_for_each_device_resource(node, "_CRS", uart_parse_resource, &crs);
    if(uacpi_unlikely_error(status)) {
        LOG_WARN("16550uart: _CRS evaluation failed: %s\n", uacpi_status_to_string(status));
        return UACPI_ITERATION_DECISION_CONTINUE;
    }

    if(crs.io_port == 0) {
        LOG_WARN("16550uart: rejecting serial device with no IO port\n");
        return UACPI_ITERATION_DECISION_CONTINUE;
    }

    LOG_INFO("16550uart: serial port on IO port 0x%04x\n", crs.io_port);
    if(crs.irq != 0xff) {
        LOG_OKAY("16550uart: interrupt on IRQ %d (polarity=%s, trigger=%s)\n", crs.irq, crs.irq_low_polarity ? "low" : "high", crs.irq_edge_triggered ? "edge" : "level");
    } else {
        LOG_WARN("16550uart: no interrupt\n");
    }

    arch_16550uart_t* uart = heap_alloc(sizeof(arch_16550uart_t));
    uart->uart_port = crs.io_port;
    uart->irq = crs.irq;
    uart->irq_edge_triggered = crs.irq_edge_triggered;
    uart->irq_low_polarity = crs.irq_low_polarity;
    uart->dw_item = dw_create(serial_rx_dw_handler, uart);
    uart->dw_item->cleanup_fn = nullptr;
    list_push_back(&g_uart_list, &uart->uart_list_node);

    return UACPI_ITERATION_DECISION_CONTINUE;
}

void arch_16550uart_setup() {
    uacpi_find_devices_at(uacpi_namespace_root(), g_uart_hids, uart_device_find_callback, nullptr);

    LIST_FOR_EACH(&g_uart_list, uart_node) {
        arch_16550uart_t* uart = CONTAINER_OF(uart_node, arch_16550uart_t, uart_list_node);
        if(uart->uart_port != g_arch_16550uart_default_uart->uart_port) {
            continue;
        }

        if(uart->irq == 0xff) {
            // @todo: pick a diffrent serial port
            LOG_WARN("16550uart: Default serial port does not support interrupt RX...\n");
            continue;
        }

        g_arch_16550uart_default_uart = uart;

        uint8_t vector = arch_interrupt_alloc_allocate();
        interrupt_set_handler(vector, serial_rx_handler, uart);

        // @todo: lapic allocation
        arch_ioapic_map_legacy_irq(uart->irq, 0, uart->irq_low_polarity, uart->irq_edge_triggered, vector);
        serial_set_interrupts(uart->uart_port, true);
    }
}
