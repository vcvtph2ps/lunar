#pragma once

// Heavily inspired and refactored from
// https://git.evalyngoemer.com/evalynOS/evalynOS/src/commit/ee92dac22b5567f597cce3c36dba44af0b87222b/kernel/src/arch/x86_64/drivers/16550uart.h

#include <arch/x86_64/io.h>
#include <common/arch.h>
#include <common/interrupts/dw.h>
#include <lib/list.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    void (*on_recv)(void* ctx, char c);
    void* recv_ctx;

    uint16_t uart_port;

    uint8_t irq;
    bool irq_low_polarity;
    bool irq_edge_triggered;

    list_node_t uart_list_node;
} arch_16550uart_t;

extern arch_16550uart_t* g_arch_16550uart_default_uart;

void arch_16550uart_early_setup();
void arch_16550uart_setup();

int arch_16550uart_transmit_empty(arch_16550uart_t* uart);
int arch_16550uart_data_ready(arch_16550uart_t* uart);
void arch_16550uart_send(arch_16550uart_t* uart, char c);
int arch_16550uart_read(arch_16550uart_t* uart);
