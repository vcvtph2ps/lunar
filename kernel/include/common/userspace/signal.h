#pragma once
#include <stdint.h>
#include <uapi/signal.h>

static inline uapi_signal_set_t signal_mask_set(uapi_signal_set_t set, int signal_number) {
    assert(signal_number > 0 && signal_number < UAPI_SIGNAL_MAX_SIGNAL);
    return set | (1 << signal_number);
}

static inline uapi_signal_set_t signal_mask_clear(uapi_signal_set_t set, int signal_number) {
    assert(signal_number > 0 && signal_number < UAPI_SIGNAL_MAX_SIGNAL);
    return set & ~(1 << signal_number);
}

static inline uapi_signal_set_t signal_mask_sanitize(uapi_signal_set_t set) {
    return set & ~(UAPI_SIGNAL_STOP | UAPI_SIGNAL_KILL);
}
