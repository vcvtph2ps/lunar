#include <nanoprintf/nanoprintf.h>

///
#include <common/arch.h>
#include <common/init.h>
#include <common/log.h>
#include <common/sync/spinlock.h>
#include <flanterm.h>
#include <flanterm_backends/fb.h>
#include <lib/math.h>
#include <lib/types.h>
#include <memory/vm.h>

// @todo: refactor this
#define MAX_LOG_SINKS 8

static log_sink_t g_sinks[MAX_LOG_SINKS];
static size_t g_sink_count = 0;
static spinlock_no_int_t g_log_lock = SPINLOCK_NO_INT_INIT;

void log_init() {
    arch_log_init();
    log_print_lockless(LOG_LEVEL_FAIL, "\n");
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

#define LOG_FORMAT_BUFFER_SIZE 256

typedef struct {
    const log_sink_t** selected_sinks;
    size_t sink_count;
    char buffer[LOG_FORMAT_BUFFER_SIZE];
    size_t length;
} log_print_ctx_t;

static void log_flush_ctx(log_print_ctx_t* ctx) {
    if(ctx->length == 0) {
        return;
    }

    for(size_t i = 0; i < ctx->sink_count; i++) {
        const log_sink_t* sink = ctx->selected_sinks[i];

        if(sink->write_str != nullptr) {
            sink->write_str(ctx->buffer, ctx->length, sink->ctx);
        } else if(sink->write != nullptr) {
            for(size_t j = 0; j < ctx->length; j++) {
                sink->write((int) (unsigned char) ctx->buffer[j], sink->ctx);
            }
        }
    }

    ctx->length = 0;
}

static void log_buffer_char(int c, void* pctx) {
    log_print_ctx_t* ctx = pctx;

    if(c == '\n') {
        if(ctx->length == sizeof(ctx->buffer)) {
            log_flush_ctx(ctx);
        }
        ctx->buffer[ctx->length++] = '\r';
    }

    if(ctx->length == sizeof(ctx->buffer)) {
        log_flush_ctx(ctx);
    }
    ctx->buffer[ctx->length++] = (char) c;
}

void log_vprint_lockless(log_level_t level, const char* fmt, va_list val) {
    const log_sink_t* selected_sinks[MAX_LOG_SINKS];
    size_t selected_sink_count = 0;
    for(size_t i = 0; i < g_sink_count; i++) {
        if(level >= g_sinks[i].min_level && (g_sinks[i].write != nullptr || g_sinks[i].write_str != nullptr)) {
            selected_sinks[selected_sink_count++] = &g_sinks[i];
        }
    }

    if(selected_sink_count == 0) {
        return;
    }

    log_print_ctx_t ctx = {
        .selected_sinks = selected_sinks,
        .sink_count = selected_sink_count,
        .length = 0,
    };

    npf_vpprintf(log_buffer_char, &ctx, fmt, val);
    log_flush_ctx(&ctx);

    for(size_t i = 0; i < selected_sink_count; i++) {
        if(selected_sinks[i]->flush != nullptr) {
            selected_sinks[i]->flush(selected_sinks[i]->ctx);
        }
    }
}

void log_print_lockless(log_level_t level, const char* fmt, ...) {
    va_list val;
    va_start(val, fmt);
    log_vprint_lockless(level, fmt, val);
    va_end(val);
}

void log_vprint(log_level_t level, const char* fmt, va_list val) {
    arch_interrupt_state_t state = spinlock_noint_lock(&g_log_lock);
    log_vprint_lockless(level, fmt, val);
    spinlock_noint_unlock(&g_log_lock, state);
}

void log_print(log_level_t level, const char* fmt, ...) {
    va_list val;
    va_start(val, fmt);
    arch_interrupt_state_t state = spinlock_noint_lock(&g_log_lock);
    log_vprint_lockless(level, fmt, val);
    spinlock_noint_unlock(&g_log_lock, state);
    va_end(val);
}

arch_interrupt_state_t log_lock_acquire(void) {
    return spinlock_noint_lock(&g_log_lock);
}

void log_lock_release(arch_interrupt_state_t state) {
    spinlock_noint_unlock(&g_log_lock, state);
}
