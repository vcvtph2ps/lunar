#include <nanoprintf/nanoprintf.h>

///
#include <arch/io.h>
#include <common/log.h>
#include <common/spinlock.h>

#include "arch/interrupt.h"
#include "common/arch.h"

// @todo: refactor this
#define MAX_LOG_SINKS 8
static log_sink_t g_sinks[MAX_LOG_SINKS];
static size_t g_sink_count = 0;

static spinlock_no_int_t g_log_lock = SPINLOCK_NO_INT_INIT;

void sink_debug(int c, void* ctx) {
    (void) ctx;
    arch_io_port_write_u8(0xe9, (uint8_t) c);
}

void log_init() {
    log_sink_t debug_sink = {
        .min_level = LOG_LEVEL_STRC,
        .write = sink_debug,
        .ctx = nullptr,
    };
    if(!log_add_sink(&debug_sink)) {
        // we really can't do much about that can we?
        while(1) {
            (void) arch_interrupt_disable();
            arch_wait_for_interrupt();
        }
    }
}

bool log_add_sink(const log_sink_t* sink) {
    arch_interrupt_state_t state = spinlock_noint_lock(&g_log_lock);
    if(g_sink_count >= MAX_LOG_SINKS) {
        spinlock_noint_unlock(&g_log_lock, state);
        return false;
    }
    g_sinks[g_sink_count++] = *sink;
    spinlock_noint_unlock(&g_log_lock, state);
    return true;
}

typedef struct {
    const log_sink_t** selected_sinks;
    const size_t sink_count;
} log_print_ctx_t;

static void dispatch_to_sinks(int c, void* ctx) {
    log_print_ctx_t* print_ctx = (log_print_ctx_t*) ctx;

    for(size_t i = 0; i < print_ctx->sink_count; i++) {
        if(print_ctx->selected_sinks[i]->write != nullptr) { print_ctx->selected_sinks[i]->write(c, print_ctx->selected_sinks[i]->ctx); }
    }
}


void log_vprint(log_level_t level, const char* fmt, va_list val) {
    log_sink_t* selected_sinks[MAX_LOG_SINKS];
    size_t selected_sink_count = 0;
    for(size_t i = 0; i < g_sink_count; i++) {
        if(level >= g_sinks[i].min_level && g_sinks[i].write != nullptr) { selected_sinks[selected_sink_count++] = &g_sinks[i]; }
    }

    if(selected_sink_count == 0) { return; }

    log_print_ctx_t ctx = {
        .selected_sinks = (const struct log_sink**) &selected_sinks,
        .sink_count = selected_sink_count,
    };

    npf_vpprintf(dispatch_to_sinks, &ctx, fmt, val);
}

void log_print(log_level_t level, const char* fmt, ...) {
    va_list val;
    va_start(val, fmt);
    log_vprint(level, fmt, val);
    va_end(val);
}
