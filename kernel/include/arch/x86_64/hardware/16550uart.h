#pragma once

// Heavily inspired and refactored from
// https://git.evalyngoemer.com/evalynOS/evalynOS/src/commit/ee92dac22b5567f597cce3c36dba44af0b87222b/kernel/src/arch/x86_64/drivers/16550uart.h

#include <arch/x86_64/io.h>
#include <common/arch.h>
#include <stdbool.h>
#include <stdint.h>

void arch_16550uart_early_setup();
int arch_16550uart_transmit_empty();
int arch_16550uart_data_ready();
void arch_16550uart_send(char c);
int arch_16550uart_read();
