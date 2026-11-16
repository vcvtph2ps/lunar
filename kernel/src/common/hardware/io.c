#include <common/arch.h>
#include <common/hardware/io.h>

#if defined(__x86_64__)
#include <arch/x86_64/io.h>
#elif defined(__riscv) && __riscv_xlen == 64
#include <arch/riscv64/io.h>
#else
#error "Unknown architecture"
#endif

#if !defined(__x86_64__)
[[noreturn]] static void io_port_unsupported(void) {
    arch_panic("port-mapped I/O is not supported on this architecture\n");
}
#endif

void io_wait(const io_region_t* region) {
    if(region->type == IO_TYPE_PORT) {
#if defined(__x86_64__)
        arch_io_wait();
#endif
    }
}

uint8_t io_read8(const io_region_t* region, size_t offset) {
    if(region->type == IO_TYPE_PORT) {
#if defined(__x86_64__)
        return arch_io_port_read_u8((uint16_t) (region->base + offset));
#else
        io_port_unsupported();
#endif
    }
    return arch_io_mem_read_u8(region->base + offset);
}

uint16_t io_read16(const io_region_t* region, size_t offset) {
    if(region->type == IO_TYPE_PORT) {
#if defined(__x86_64__)
        return arch_io_port_read_u16((uint16_t) (region->base + offset));
#else
        io_port_unsupported();
#endif
    }
    return arch_io_mem_read_u16(region->base + offset);
}

uint32_t io_read32(const io_region_t* region, size_t offset) {
    if(region->type == IO_TYPE_PORT) {
#if defined(__x86_64__)
        return arch_io_port_read_u32((uint16_t) (region->base + offset));
#else
        io_port_unsupported();
#endif
    }
    return arch_io_mem_read_u32(region->base + offset);
}

uint64_t io_read64(const io_region_t* region, size_t offset) {
    if(region->type == IO_TYPE_PORT) {
        // there is no such thing as a 64-bit port read; emulate it with two 32-bit reads.
        uint64_t low = io_read32(region, offset);
        uint64_t high = io_read32(region, offset + 4);
        return low | (high << 32);
    }
    return arch_io_mem_read_u64(region->base + offset);
}

void io_write8(const io_region_t* region, size_t offset, uint8_t value) {
    if(region->type == IO_TYPE_PORT) {
#if defined(__x86_64__)
        arch_io_port_write_u8((uint16_t) (region->base + offset), value);
        return;
#else
        io_port_unsupported();
#endif
    }
    arch_io_mem_write_u8(region->base + offset, value);
}

void io_write16(const io_region_t* region, size_t offset, uint16_t value) {
    if(region->type == IO_TYPE_PORT) {
#if defined(__x86_64__)
        arch_io_port_write_u16((uint16_t) (region->base + offset), value);
        return;
#else
        io_port_unsupported();
#endif
    }
    arch_io_mem_write_u16(region->base + offset, value);
}

void io_write32(const io_region_t* region, size_t offset, uint32_t value) {
    if(region->type == IO_TYPE_PORT) {
#if defined(__x86_64__)
        arch_io_port_write_u32((uint16_t) (region->base + offset), value);
        return;
#else
        io_port_unsupported();
#endif
    }
    arch_io_mem_write_u32(region->base + offset, value);
}

void io_write64(const io_region_t* region, size_t offset, uint64_t value) {
    if(region->type == IO_TYPE_PORT) {
        io_write32(region, offset, (uint32_t) value);
        io_write32(region, offset + 4, (uint32_t) (value >> 32));
        return;
    }
    arch_io_mem_write_u64(region->base + offset, value);
}
