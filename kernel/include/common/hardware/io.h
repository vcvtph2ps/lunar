#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum {
    /// @brief X86 port mapped I/O, the region base is a port number
    IO_TYPE_PORT,
    /// @brief Memory mapped I/O. the region base is a virtual address
    IO_TYPE_MMIO,
} io_type_t;

/// @brief A region of I/O space that can be accessed either through port I/O or
typedef struct {
    io_type_t type;
    /// @brief Port number (IO_TYPE_PORT) or virtual base address (IO_TYPE_MMIO)
    uintptr_t base;
} io_region_t;

#define IO_REGION_PORT(PORT) ((io_region_t) { .type = IO_TYPE_PORT, .base = (PORT) })
#define IO_REGION_MMIO(ADDR) ((io_region_t) { .type = IO_TYPE_MMIO, .base = (uintptr_t) (ADDR) })

/**
 * @brief Inserts a small delay for slow I/O devices
 * @note This is a no-op for memory mapped regions
 */
void io_wait(const io_region_t* region);

[[nodiscard]] uint8_t io_read8(const io_region_t* region, size_t offset);
[[nodiscard]] uint16_t io_read16(const io_region_t* region, size_t offset);
[[nodiscard]] uint32_t io_read32(const io_region_t* region, size_t offset);
[[nodiscard]] uint64_t io_read64(const io_region_t* region, size_t offset);
void io_write8(const io_region_t* region, size_t offset, uint8_t value);
void io_write16(const io_region_t* region, size_t offset, uint16_t value);
void io_write32(const io_region_t* region, size_t offset, uint32_t value);
void io_write64(const io_region_t* region, size_t offset, uint64_t value);
