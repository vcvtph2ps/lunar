#include <arch/hardware/pit.h>
#include <arch/io.h>
#include <stdint.h>

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND 0x43

#define PIT_FREQUENCY 1193182ULL // PIT base frequency (Hz)
#define PIT_MODE_BINARY 0x00
#define PIT_MODE_RATE 0x04
#define PIT_ACCESS_LOHI 0x30
#define PIT_CHANNEL0_SEL 0x00

static inline void pit_set_reload(uint16_t reload_value) {
    arch_io_port_write_u8(PIT_COMMAND, PIT_CHANNEL0_SEL | PIT_ACCESS_LOHI | PIT_MODE_RATE | PIT_MODE_BINARY);
    arch_io_port_write_u8(PIT_CHANNEL0, reload_value & 0xFF);
    arch_io_port_write_u8(PIT_CHANNEL0, (reload_value >> 8) & 0xFF);
}

void arch_pit_sleep_us(uint64_t microseconds) {
    if(microseconds == 0) return;

    uint64_t total_ticks = (PIT_FREQUENCY * microseconds) / 1000000ULL;
    if(total_ticks == 0) total_ticks = 1;

    while(total_ticks > 0) {
        uint16_t chunk = (total_ticks > 0xFFFF) ? 0xFFFF : (uint16_t) total_ticks;
        total_ticks -= chunk;

        pit_set_reload(chunk);

        uint16_t last = 0xFFFF;
        while(1) {
            arch_io_port_write_u8(PIT_COMMAND, 0x00);
            uint8_t lo = arch_io_port_read_u8(PIT_CHANNEL0);
            uint8_t hi = arch_io_port_read_u8(PIT_CHANNEL0);
            uint16_t current = ((uint16_t) hi << 8) | lo;
            if(current > last) break;
            last = current;
        }
    }
}
