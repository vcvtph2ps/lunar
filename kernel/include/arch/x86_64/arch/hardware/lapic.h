#pragma once
#include <stdint.h>

/**
 * @brief Initializes the LAPIC for the current core
 * @param core_id The ID of the current core
 */
void arch_lapic_init(uint32_t core_id);

/**
 * @brief Get the APIC ID of the current processor
 * @return The APIC ID of the current processor
 */
uint32_t arch_lapic_get_id();

/**
 * @brief Checks if the current processor is the bootstrap processor
 * @returns True if the current process is the bootstrap processor
 */
bool arch_lapic_is_bsp();

/**
 * @brief Sends end of interrupt to the LAPIC
 */
void arch_lapic_eoi();

/**
 * @brief Sends an interprocessor interrupt with the specified vector to the processor with the specified APIC ID.
 * @param apic_id The APIC ID of the target processor to send the IPI
 * @param vector The interrupt vector to send in the IPI.
 */
void arch_lapic_send_ipi(uint32_t apic_id, uint8_t vector);

/**
 * @brief Broadcasts an interprocessor interrupt with the specified vector to all other processors in the system.
 * @param vector The interrupt vector to send in the IPI.
 */
void arch_lapic_broadcast_ipi(uint8_t vector);
