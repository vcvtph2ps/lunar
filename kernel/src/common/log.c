#include <nanoprintf/nanoprintf.h>

///
#include <common/arch.h>
#include <common/init.h>
#include <common/log.h>
#include <common/sync/spinlock.h>
#include <flanterm.h>
#include <flanterm_backends/fb.h>

// @todo: refactor this
#define MAX_LOG_SINKS 8
static log_sink_t g_sinks[MAX_LOG_SINKS];
static size_t g_sink_count = 0;
static bool g_log_to_framebuffer = true;
static spinlock_no_int_t g_log_lock = SPINLOCK_NO_INT_INIT;

void log_init() {
    arch_log_init();
    log_print_lockless(LOG_LEVEL_FAIL, "\n");
}

static void framebuffer_sink(int c, void* ctx) {
    struct flanterm_context* ft_ctx = (struct flanterm_context*) ctx;
    if(c == '\n') { flanterm_write(ft_ctx, "\r", 1); }
    flanterm_write(ft_ctx, (const char*) &c, 1);
}

void log_framebuffer_reinit() {
    for(size_t i = 0; i < g_sink_count; i++) {
        if(g_sinks[i].is_framebuffer) {
            struct flanterm_context* ft_ctx = (struct flanterm_context*) g_sinks[i].ctx;
            flanterm_fb_update_framebuffer(ft_ctx, g_init_boot_info->framebuffers[g_sinks[i].framebuffer_index].vaddr);
        }
    }
}

void log_framebuffer_enable(bool enable) {
    arch_interrupt_state_t state = spinlock_noint_lock(&g_log_lock);
    g_log_to_framebuffer = enable;
    spinlock_noint_unlock(&g_log_lock, state);
}

void log_framebuffer_init() {
    for(size_t i = 0; i < g_init_boot_info->framebuffer_count; i++) {
        bootinfo_framebuffer_t framebuffer = g_init_boot_info->framebuffers[i];
        if(framebuffer.vaddr != nullptr) {
            struct flanterm_context* ft_ctx = flanterm_fb_init(
                nullptr,
                nullptr,
                framebuffer.vaddr,
                framebuffer.width,
                framebuffer.height,
                framebuffer.pitch,
                framebuffer.red_size,
                framebuffer.red_position,
                framebuffer.green_size,
                framebuffer.green_position,
                framebuffer.blue_size,
                framebuffer.blue_position,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                0,
                0,
                1,
                0,
                0,
                0,
                0
            );
            log_sink_t sink = { .min_level = LOG_LEVEL_INFO, .write = framebuffer_sink, .ctx = ft_ctx, .is_framebuffer = true, .framebuffer_index = i };
            if(log_add_sink(&sink)) {
                LOG_OKAY("Framebuffer logging initialized for framebuffer %zu\n", i);
            } else {
                LOG_FAIL("Failed to add framebuffer logging for framebuffer %zu\n", i);
            }
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
    log_print_ctx_t* print_ctx = ctx;

    for(size_t i = 0; i < print_ctx->sink_count; i++) {
        const log_sink_t* sink = print_ctx->selected_sinks[i];

        if(sink->write == nullptr) { continue; }

        if(sink->is_framebuffer && !g_log_to_framebuffer) { continue; }

        sink->write(c, sink->ctx);
    }
}

void log_vprint_lockless(log_level_t level, const char* fmt, va_list val) {
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
