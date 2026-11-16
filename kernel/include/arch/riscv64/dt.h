#pragma once
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Gets the HHDM mapped device tree blob passed by the bootloader.
 * @return A pointer to the device tree blob, or nullptr if none was provided or it is invalid.
 */
[[nodiscard]] const void* arch_dt_blob(void);

/**
 * @brief Finds the first node whose "compatible" property matches any of the given strings.
 * @param fdt The device tree blob.
 * @param start_offset The offset to start searching from, or -1 to search from the root.
 * @param compatibles A nullptr terminated array of compatible strings.
 * @return The node offset, or a negative libfdt error code if none matched.
 */
[[nodiscard]] int arch_dt_node_offset_by_compatible_list(const void* fdt, int start_offset, const char* const compatibles[]);

/**
 * @brief Reads the first (address, size) pair of a node's "reg" property.
 * @param fdt The device tree blob.
 * @param node_offset The node to read from.
 * @param address Output for the region base address.
 * @param size Output for the region size, may be nullptr.
 * @return true if the region was parsed, false otherwise.
 */
[[nodiscard]] bool arch_dt_get_reg(const void* fdt, int node_offset, uint64_t* address, uint64_t* size);

/**
 * @brief Reads the first cell of a node's "interrupts" property.
 * @param fdt The device tree blob.
 * @param node_offset The node to read from.
 * @param interrupt Output for the interrupt specifier's first cell.
 * @return true if the interrupt was parsed, false otherwise.
 */
[[nodiscard]] bool arch_dt_get_interrupt(const void* fdt, int node_offset, uint32_t* interrupt);
