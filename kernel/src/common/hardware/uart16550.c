// Heavily inspired and refactored from
// https://git.evalyngoemer.com/evalynOS/evalynOS/src/commit/ee92dac22b5567f597cce3c36dba44af0b87222b/kernel/src/arch/x86_64/drivers/uart16550.c

#include <common/arch.h>
#include <common/hardware/uart16550.h>
#include <common/interrupts/dpc.h>
#include <common/log.h>
#include <common/sched/sched.h>
#include <common/sync/wait_obj.h>
#include <lib/helpers.h>
#include <memory/heap.h>
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
#define SERIAL_FIFO_ENABLE 0x01 // bit 0 set
#define SERIAL_FIFO_RX_FLUSH 0x02 // bit 1 set
#define SERIAL_FIFO_TX_FLUSH 0x04 // bit 2 set

/* Line Control Register */
#define SERIAL_LCR_8BIT 0x03 // bit 0 & 1 set
#define SERIAL_LCR_1STOP 0x00 // bit 2 unset
#define SERIAL_LCR_PARITY_NONE 0x00 // bit 3 & 4 & 5 unset

/* Modem Control Register */
#define SERIAL_MCR_TX_ENABLE 0x01 // bit 0 set (DTR)
#define SERIAL_MCR_RX_ENABLE 0x02 // bit 1 set (RTS)
#define SERIAL_MCR_IRQ_ENABLE 0x08 // bit 3 set
#define SERIAL_MCR_LOOP_ENABLE 0x10 // bit 4 set

/* Misc */
#define SERIAL_DLAB_BIT 0x80
#define SERIAL_DATA_READY_BIT 0x01
#define SERIAL_TX_EMPTY_BIT 0x20
#define SERIAL_TEST_MAGIC 0x69
#define SERIAL_TEST_RETRIES 5

uart16550_t* g_uart16550_default = nullptr;
bool g_uart16550_works = false;

list_t g_uart16550_list = LIST_INIT;

static uint8_t serial_reg_read(const io_region_t* io, uint8_t reg) {
    io_wait(io);
    uint8_t value = io_read8(io, reg);
    io_wait(io);
    return value;
}

static void serial_reg_write(const io_region_t* io, uint8_t reg, uint8_t value) {
    io_wait(io);
    io_write8(io, reg, value);
    io_wait(io);
}

static void serial_set_dlab(const io_region_t* io, bool setting) {
    uint8_t lcr = serial_reg_read(io, SERIAL_LINE_CONF);
    if(setting) {
        serial_reg_write(io, SERIAL_LINE_CONF, lcr | SERIAL_DLAB_BIT);
    } else {
        serial_reg_write(io, SERIAL_LINE_CONF, lcr & ~SERIAL_DLAB_BIT);
    }
}

static void serial_set_divisor(const io_region_t* io, uint16_t divisor) {
    serial_set_dlab(io, true);
    serial_reg_write(io, SERIAL_DLAB_DIV_LO, divisor & 0xff);
    serial_reg_write(io, SERIAL_DLAB_DIV_HI, (divisor >> 8) & 0xff);
    serial_set_dlab(io, false);
}

static void serial_set_interrupts(const io_region_t* io, uint8_t setting) {
    serial_set_dlab(io, false);
    serial_reg_write(io, SERIAL_INTR_CONF, setting);
}

static void serial_set_mcr(const io_region_t* io, uint8_t setting) {
    serial_reg_write(io, SERIAL_MODEM_CONF, setting);
}

static void serial_set_lcr(const io_region_t* io, uint8_t lcr) {
    serial_reg_write(io, SERIAL_LINE_CONF, lcr);
}

static void serial_set_fifo(const io_region_t* io, uint8_t fifo) {
    serial_reg_write(io, SERIAL_FIFO_CONF, fifo);
}

static int uart16550_probe(const io_region_t* io) {
    serial_set_divisor(io, UART16550_DIVISOR_115200);
    serial_set_lcr(io, SERIAL_LCR_8BIT | SERIAL_LCR_1STOP | SERIAL_LCR_PARITY_NONE);
    serial_set_fifo(io, SERIAL_FIFO_TX_FLUSH | SERIAL_FIFO_RX_FLUSH);
    serial_set_mcr(io, SERIAL_MCR_TX_ENABLE | SERIAL_MCR_RX_ENABLE | SERIAL_MCR_LOOP_ENABLE);
    serial_set_dlab(io, false);
    for(int i = 0; i < SERIAL_TEST_RETRIES; i++) {
        serial_reg_write(io, SERIAL_TX_BUFF, SERIAL_TEST_MAGIC);
        for(int j = 0; j < 256; j++) {
            arch_spin_hint();
        }

        if(serial_reg_read(io, SERIAL_RX_BUFF) == SERIAL_TEST_MAGIC) {
            return 2;
        }
    }

    // fallback test to just make sure it exists at all
    serial_reg_write(io, SERIAL_SCRATCH_REG, SERIAL_TEST_MAGIC);
    if(serial_reg_read(io, SERIAL_SCRATCH_REG) == SERIAL_TEST_MAGIC) {
        return 1;
    }
    return 0;
}

bool uart16550_init(uart16550_t* uart, uint16_t divisor) {
    int status = uart16550_probe(&uart->io);
    if(status == 0) {
        return false;
    }

    serial_set_interrupts(&uart->io, false);
    serial_set_divisor(&uart->io, divisor);
    serial_set_lcr(&uart->io, SERIAL_LCR_8BIT | SERIAL_LCR_1STOP | SERIAL_LCR_PARITY_NONE);
    serial_set_fifo(&uart->io, SERIAL_FIFO_ENABLE | SERIAL_FIFO_THRESH_1B | SERIAL_FIFO_TX_FLUSH | SERIAL_FIFO_RX_FLUSH);
    serial_set_mcr(&uart->io, SERIAL_MCR_TX_ENABLE | SERIAL_MCR_RX_ENABLE | SERIAL_MCR_IRQ_ENABLE);
    serial_set_dlab(&uart->io, false);

    if(status == 1) {
        LOG_WARN("UART at I/O region 0x%lx failed part of self test\n", (unsigned long) uart->io.base);
        return false;
    }

    uart->rx_head = 0;
    uart->rx_tail = 0;
    return true;
}

uart16550_t* uart16550_create(const io_region_t* io, uint32_t irq, bool irq_low_polarity, bool irq_edge_triggered) {
    uart16550_t* uart = heap_zalloc(sizeof(uart16550_t));
    if(uart == nullptr) {
        return nullptr;
    }

    uart->io = *io;
    uart->irq = irq;
    uart->irq_low_polarity = irq_low_polarity;
    uart->irq_edge_triggered = irq_edge_triggered;
    return uart;
}

void uart16550_register(uart16550_t* uart) {
    list_push_back(&g_uart16550_list, &uart->uart_list_node);
}

void uart16550_set_default(uart16550_t* uart) {
    g_uart16550_default = uart;
}

int uart16550_transmit_empty(uart16550_t* uart) {
    return serial_reg_read(&uart->io, SERIAL_LINE_INFO) & SERIAL_TX_EMPTY_BIT;
}

int uart16550_data_ready(uart16550_t* uart) {
    uint8_t data = serial_reg_read(&uart->io, SERIAL_LINE_INFO) & SERIAL_DATA_READY_BIT;
    return data;
}

void uart16550_send(uart16550_t* uart, char c) {
    if(c == '\n') {
        uart16550_send(uart, '\r');
    }

    for(int i = 0; i < 100000; i++) {
        if(uart16550_transmit_empty(uart)) {
            break;
        }
        arch_spin_hint();
    }
    serial_reg_write(&uart->io, SERIAL_TX_BUFF, (uint8_t) c);
}

int uart16550_read(uart16550_t* uart) {
    if(!uart16550_data_ready(uart)) {
        return -1;
    }

    uint8_t data = serial_reg_read(&uart->io, SERIAL_RX_BUFF);
    return data;
}

static void serial_sink(int c, void* ctx) {
    (void) ctx;
    if(g_uart16550_default != nullptr) {
        uart16550_send(g_uart16550_default, (char) c);
    }
}

static void serial_sink_str(const char* s, size_t len, void* ctx) {
    (void) ctx;
    if(g_uart16550_default == nullptr) {
        return;
    }
    for(size_t i = 0; i < len; i++) {
        uart16550_send(g_uart16550_default, s[i]);
    }
}

void uart16550_set_rx_interrupt(uart16550_t* uart, bool enabled) {
    serial_set_interrupts(&uart->io, enabled ? 0x01 : 0x00);
}

bool uart16550_add_log_sink() {
    log_sink_t sink = {
        .min_level = LOG_LEVEL_STRC,
        .write = serial_sink,
        .write_str = serial_sink_str,
        .ctx = nullptr,
    };

    return log_add_sink(&sink);
}

static wait_obj_t g_uart16550_wait_obj = WAIT_OBJ_INIT_SYNCHRONIZATION();

static dpc_t* g_uart16550_rx_dpc = nullptr;

static void uart16550_rx_dpc_handler(void* ctx) {
    (void) ctx;
    wait_obj_signal(&g_uart16550_wait_obj, 1);
}

void uart16550_handle_interrupt(uart16550_t* uart) {
    int c;
    while((c = uart16550_read(uart)) >= 0) {
        size_t head = ATOMIC_LOAD(&uart->rx_head, ATOMIC_RELAXED);
        size_t next = (head + 1) % UART16550_RX_BUFFER_SIZE;
        if(next == ATOMIC_LOAD(&uart->rx_tail, ATOMIC_ACQUIRE)) {
            // ring buffer is full, but still drain the buffer
            continue;
        }
        uart->rx_buffer[head] = (uint8_t) c;
        ATOMIC_STORE(&uart->rx_head, next, ATOMIC_RELEASE);
    }

    if(g_uart16550_rx_dpc != nullptr) {
        dpc_queue(g_uart16550_rx_dpc);
    }
}

[[noreturn]] static void uart16550_rx_thread() {
    while(1) {
        uart16550_t* uart = g_uart16550_default;
        if(uart == nullptr) {
            sched_wait_single(&g_uart16550_wait_obj, UINT64_MAX);
            continue;
        }

        size_t tail = ATOMIC_LOAD(&uart->rx_tail, ATOMIC_RELAXED);
        size_t head = ATOMIC_LOAD(&uart->rx_head, ATOMIC_ACQUIRE);
        while(tail != head) {
            char c = (char) uart->rx_buffer[tail];
            tail = (tail + 1) % UART16550_RX_BUFFER_SIZE;
            ATOMIC_STORE(&uart->rx_tail, tail, ATOMIC_RELEASE);

            if(uart->on_recv != nullptr) {
                uart->on_recv(uart->recv_ctx, c);
            }

            head = ATOMIC_LOAD(&uart->rx_head, ATOMIC_ACQUIRE);
        }

        sched_wait_single(&g_uart16550_wait_obj, UINT64_MAX);
    }
}

bool uart16550_start_rx_worker() {
    if(g_uart16550_default == nullptr) {
        return false;
    }

    if(g_uart16550_rx_dpc == nullptr) {
        g_uart16550_rx_dpc = dpc_create(uart16550_rx_dpc_handler, nullptr);
        if(g_uart16550_rx_dpc == nullptr) {
            return false;
        }
    }

    thread_t* thread = sched_arch_create_kernel_thread((virt_addr_t) uart16550_rx_thread);
    if(thread == nullptr) {
        return false;
    }

    sched_thread_schedule(thread);
    return true;
}
