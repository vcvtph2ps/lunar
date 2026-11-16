#include <arch/riscv64/cpu_local.h>
#include <arch/riscv64/hardware/time/rdtime.h>
#include <arch/riscv64/internal/csr.h>
#include <common/arch.h>
#include <common/assert.h>
#include <common/time/time.h>
#include <stdint.h>

#define MICROSECONDS_PER_SECOND 1000000ULL

static time_timer_t g_rdtime_timer;

[[nodiscard]] static uint64_t rdtime_read(void) {
    return ARCH_CSR_READ(time);
}

/// @brief scales value by the timebase frequency without overflowing a 64 bit integer
[[nodiscard]] static uint64_t rdtime_microseconds_to_ticks(uint64_t microseconds, uint64_t frequency) {
    return (microseconds / MICROSECONDS_PER_SECOND) * frequency + ((microseconds % MICROSECONDS_PER_SECOND) * frequency) / MICROSECONDS_PER_SECOND;
}

static void rdtime_sleep(time_timer_t* self, uint64_t microseconds) {
    (void) self;

    uint64_t frequency = CPU_LOCAL_READ(timebase_frequency);
    assert(frequency != 0);

    uint64_t deadline = rdtime_read() + rdtime_microseconds_to_ticks(microseconds, frequency);

    while(rdtime_read() < deadline) {
        arch_spin_hint();
    }
}

static uint64_t rdtime_read_raw(time_timer_t* self) {
    (void) self;
    return rdtime_read();
}

static uint64_t rdtime_read_microseconds(time_timer_t* self) {
    (void) self;

    uint64_t frequency = CPU_LOCAL_READ(timebase_frequency);
    assert(frequency != 0);

    uint64_t ticks = rdtime_read();
    return (ticks / frequency) * MICROSECONDS_PER_SECOND + ((ticks % frequency) * MICROSECONDS_PER_SECOND) / frequency;
}

time_timer_t* arch_rdtime_timer_init(void) {
    if(CPU_LOCAL_READ(timebase_frequency) == 0) {
        return nullptr;
    }

    g_rdtime_timer = (time_timer_t) {
        .score = 50,
        .name = "rdtime",
        .sleep = rdtime_sleep,
        .read_raw = rdtime_read_raw,
        .read_microseconds = rdtime_read_microseconds,
        .exclusive = false,
        .private = nullptr,
    };

    return &g_rdtime_timer;
}
