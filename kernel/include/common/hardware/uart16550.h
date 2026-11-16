#pragma once

#include <common/hardware/io.h>
#include <common/interrupts/interrupt.h>
#include <lib/list.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/// @brief Size of the ring buffer
#define UART16550_RX_BUFFER_SIZE 256

/// @brief Divisor values for the standard baud rates
#define UART16550_DIVISOR_115200 1
#define UART16550_DIVISOR_57600 2
#define UART16550_DIVISOR_38400 3
#define UART16550_DIVISOR_19200 6
#define UART16550_DIVISOR_9600 12
#define UART16550_DIVISOR_4800 24
#define UART16550_DIVISOR_2400 48
#define UART16550_DIVISOR_1200 96
#define UART16550_DIVISOR_600 192
#define UART16550_DIVISOR_300 384

/// @ref uart16550::irq.
#define UART16550_IRQ_NONE 0xFFFFFFFFu

typedef struct uart16550 {
    /// @brief Callback invoked for every byte drained from the receive buffer, may be nullptr.
    void (*on_recv)(void* ctx, char c);
    void* recv_ctx;

    io_region_t io;

    /// @brief Arch specific interrupt number or @ref UART16550_IRQ_NONE.
    uint32_t irq;
    bool irq_low_polarity;
    bool irq_edge_triggered;

    uint8_t rx_buffer[UART16550_RX_BUFFER_SIZE];
    size_t rx_head;
    size_t rx_tail;

    list_node_t uart_list_node;
} uart16550_t;

/// @brief The default UART used for console output and input, or nullptr if none
extern uart16550_t* g_uart16550_default;

/// @brief Whether the default UART is usable
extern bool g_uart16550_works;

/// @brief Every UART discovered so far
extern list_t g_uart16550_list;

/**
 * @brief Performs early boot UART discovery
 * @note this runs before ACPI is available so that early logging works.
 */
void uart16550_early_setup();

/**
 * @brief Completes UART setup, setting up RX if available
 */
void uart16550_setup();

/**
 * @brief Initializes a UART
 * @param uart The UART to initialize.
 * @param divisor The baud rate divisor to program, see the UART16550_DIVISOR_* constants
 * @return true if the UART was found and configured, false otherwise
 */
[[nodiscard]] bool uart16550_init(uart16550_t* uart, uint16_t divisor);

/**
 * @brief Allocates and registers a new UART backed by the given I/O region
 * @param io The register block backing the UART
 * @param irq The arch specific interrupt number, or @ref UART16550_IRQ_NONE
 * @param irq_low_polarity Whether the interrupt is active low
 * @param irq_edge_triggered Whether the interrupt is edge triggered
 * @return The newly created UART, or nullptr if allocation failed
 * @note the returned UART must still be initialized with @ref uart16550_init
 */
uart16550_t* uart16550_create(const io_region_t* io, uint32_t irq, bool irq_low_polarity, bool irq_edge_triggered);


/// @brief Appends a UART to the global UART list
void uart16550_register(uart16550_t* uart);


/// @brief Marks the given UART as the default console UART
void uart16550_set_default(uart16550_t* uart);


/// @brief Checks whether the transmit holding register is empty
[[nodiscard]] int uart16550_transmit_empty(uart16550_t* uart);


/// @brief Checks whether a received byte is waiting in the FIFO
[[nodiscard]] int uart16550_data_ready(uart16550_t* uart);


/// @brief Transmits a single byte, translating '\n' into '\r\n'
void uart16550_send(uart16550_t* uart, char c);

/**
 * @brief Reads a byte from the FIFO if one is available
 * @return The byte read, or -1 if the FIFO is empty
 */
[[nodiscard]] int uart16550_read(uart16550_t* uart);

/**
 * @brief Drains the receive FIFO into the software ring buffer and wakes the receive worker
 * @note should be called from the serial hardirq
 */
void uart16550_handle_interrupt(uart16550_t* uart);

/**
 * @brief Enables or disables the UART's rx interrupt
 * @param uart The UART to configure
 * @param enabled Whether the receive interrupt should be enabled
 */
void uart16550_set_rx_interrupt(uart16550_t* uart, bool enabled);

/**
 * @brief Adds a log sink that writes to the default UART
 * @return true if the sink was added, false otherwise
 */
[[nodiscard]] bool uart16550_add_log_sink(void);

/**
 * @brief Starts the kernel thread that forwards received bytes to the default UART's on_recv callback
 * @return true if the worker was started
 */
[[nodiscard]] bool uart16550_start_rx_worker(void);
