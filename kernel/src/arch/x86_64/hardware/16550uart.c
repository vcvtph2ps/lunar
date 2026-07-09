// Heavily inspired and refactored from
// https://git.evalyngoemer.com/evalynOS/evalynOS/src/commit/ee92dac22b5567f597cce3c36dba44af0b87222b/kernel/src/arch/x86_64/drivers/16550uart.c

#include <arch/x86_64/hardware/16550uart.h>
#include <arch/x86_64/io.h>
#include <common/log.h>
#include <stdbool.h>
#include <stdint.h>

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
uint16_t g_arch_16550uart_port = 0x3F8;
bool g_arch_16550uart_enabled = true;
bool g_arch_16550uart_works = false;

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

int arch_16550uart_transmit_empty() {
    arch_io_wait();
    uint8_t data = arch_io_port_read_u8(g_arch_16550uart_port + SERIAL_LINE_INFO) & SERIAL_TX_EMPTY_BIT;
    arch_io_wait();
    return data;
}

int arch_16550uart_data_ready() {
    arch_io_wait();
    uint8_t data = arch_io_port_read_u8(g_arch_16550uart_port + SERIAL_LINE_INFO) & SERIAL_DATA_READY_BIT;
    arch_io_wait();
    return data;
}

void arch_16550uart_send(char c) {
    for(int i = 0; i < 100000; i++) {
        if(arch_16550uart_transmit_empty()) break;
        arch_spin_hint();
    }
    arch_io_wait();
    arch_io_port_write_u8(g_arch_16550uart_port + SERIAL_TX_BUFF, c);
    arch_io_wait();
}

int arch_16550uart_read() {
    if(!arch_16550uart_data_ready()) return -1;

    arch_io_wait();
    uint8_t data = arch_io_port_read_u8(g_arch_16550uart_port + SERIAL_RX_BUFF);
    arch_io_wait();
    return data;
}

static void serial_sink(int c, void* ctx) {
    (void) ctx;
    arch_16550uart_send((char) c);
}

void arch_16550uart_early_setup() {
    serial_set_interrupts(g_arch_16550uart_port, false);
    int status = serial_test(g_arch_16550uart_port);
    if(status == 0) {
        g_arch_16550uart_works = false;
        LOG_WARN("16550uart Failed to init; Do you lack a serial port at I/O port 0x%x?\n", g_arch_16550uart_port);
        return;
    }

    serial_set_divisor(g_arch_16550uart_port, SERIAL_115200_BAUD);
    serial_set_lcr(g_arch_16550uart_port, SERIAL_LCR_8BIT | SERIAL_LCR_1STOP | SERIAL_LCR_PARITY_NONE);
    serial_set_fifo(g_arch_16550uart_port, SERIAL_FIFO_ENABLE | SERIAL_FIFO_THRESH_1B | SERIAL_FIFO_TX_FLUSH | SERIAL_FIFO_RX_FLUSH);
    serial_set_mcr(g_arch_16550uart_port, SERIAL_MCR_TX_ENABLE | SERIAL_MCR_RX_ENABLE | SERIAL_MCR_IRQ_ENABLE);
    serial_set_dlab(g_arch_16550uart_port, false);
    g_arch_16550uart_works = true;

    if(status == 1) {
        LOG_FAIL("Serial port at I/O port 0x%x failed part of self test\n", g_arch_16550uart_port);
        return;
    }

    LOG_OKAY("Serial init\n");

    log_sink_t sink = {
        .min_level = LOG_LEVEL_STRC,
        .write = serial_sink,
        .ctx = nullptr,
    };

    if(!log_add_sink(&sink)) { LOG_OKAY("Failed to add log sink; serial output will not work :(\n"); }

    serial_sink('\n', nullptr);
}
