#pragma once
#include <stdint.h>

/**
 * @brief Reads the "timebase-frequency" property from the /cpus node of the boot device tree.
 * @return The timebase frequency in Hz, or 0 if it could not be determined.
 */
[[nodiscard]] uint64_t arch_dt_get_timebase_frequency(void);
