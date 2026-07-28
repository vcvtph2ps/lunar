#pragma once

#include <stdint.h>
#include <uacpi/acpi.h>

/**
 * @brief Initialize ioapics
 */
void arch_ioapic_init();

/**
 * @brief Map GSI to interrupt vector
 * @param gsi Global System Interrupt number to map
 * @param lapic_id lapic id to send the interrupt to
 * @param low_polarity true = low active, false = high active
 * @param trigger_mode true = edge sensitive, false = level sensitive
 * @param vector Interrupt vector to map the IRQ to
 */
void arch_ioapic_map_gsi(uint8_t gsi, uint8_t lapic_id, bool low_polarity, bool trigger_mode, uint8_t vector);

/**
 * @brief Map legacy IRQ to interrupt vector
 * @param irq IRQ number to map
 * @param lapic_id lapic id to send the interrupt to
 * @param fallback_low_polarity true = low active, false = high active
 * @param fallback_trigger_mode true = edge sensitive, false = level sensitive
 * @param vector Interrupt vector to map the IRQ to
 */
void arch_ioapic_map_legacy_irq(uint8_t irq, uint8_t lapic_id, bool fallback_low_polarity, bool fallback_trigger_mode, uint8_t vector);
