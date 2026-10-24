#include <nanoprintf/nanoprintf.h>

///
#include <common/arch.h>
#include <common/framebuffer.h>
#include <common/init.h>
#include <common/log.h>
#include <common/sync/spinlock.h>
#include <flanterm.h>
#include <flanterm_backends/fb.h>
#include <lib/helpers.h>
#include <lib/list.h>
#include <lib/math.h>
#include <lib/types.h>
#include <memory/heap.h>
#include <memory/pmm.h>
#include <memory/vm.h>

static void* log_fb_alloc(size_t size) {
    if(size == 0) {
        return nullptr;
    }
    return vm_map_anon(g_vm_global_address_space, VM_NO_HINT, ALIGN_UP(size, PAGE_SIZE_DEFAULT), VM_PROT_RW, VM_CACHE_NORMAL, VM_FLAG_NONE);
}

static void log_fb_free(void* pointer, size_t size) {
    if(pointer == nullptr) {
        return;
    }
    vm_unmap(g_vm_global_address_space, pointer, ALIGN_UP(size, PAGE_SIZE_DEFAULT));
}

typedef struct {
    struct flanterm_context* ft_ctx;
    list_node_t node;
} framebuffer_t;

static spinlock_no_int_t g_framebuffer_lock = SPINLOCK_NO_INT_INIT;
static list_t g_framebuffer_list = LIST_INIT;
static bool g_log_to_framebuffers = false;

static void framebuffer_sink(int c, void* ctx) {
    (void) ctx;
    if(!g_log_to_framebuffers)
        return;
    framebuffer_put_char(c);
}

static void framebuffer_sink_str(const char* s, size_t len, void* ctx) {
    (void) ctx;
    if(!g_log_to_framebuffers)
        return;
    framebuffer_put_string(s, len);
}

void framebuffer_flush(void) {
    arch_memory_fence(ARCH_MEMORY_FENCE_STORE);
}

static void framebuffer_sink_flush(void* ctx) {
    (void) ctx;
    if(!g_log_to_framebuffers)
        return;
    framebuffer_flush();
}

void framebuffer_put_char(char c) {
    arch_interrupt_state_t state = spinlock_noint_lock(&g_framebuffer_lock);

    LIST_FOR_EACH(&g_framebuffer_list, node) {
        framebuffer_t* framebuffer = CONTAINER_OF(node, framebuffer_t, node);
        if(c == '\n') {
            flanterm_write(framebuffer->ft_ctx, "\r", 1);
        }
        flanterm_write(framebuffer->ft_ctx, (const char*) &c, 1);
    };

    spinlock_noint_unlock(&g_framebuffer_lock, state);
}

void framebuffer_put_string(const char* s, size_t len) {
    if(len == 0) {
        return;
    }

    arch_interrupt_state_t state = spinlock_noint_lock(&g_framebuffer_lock);

    LIST_FOR_EACH(&g_framebuffer_list, node) {
        framebuffer_t* framebuffer = CONTAINER_OF(node, framebuffer_t, node);
        flanterm_write(framebuffer->ft_ctx, s, len);
    };

    spinlock_noint_unlock(&g_framebuffer_lock, state);
}

void framebuffer_enable(bool enable) {
    g_log_to_framebuffers = enable;
}

void framebuffer_init() {
    g_log_to_framebuffers = true;

    for(size_t i = 0; i < g_init_boot_info->framebuffer_count; i++) {
        bootinfo_framebuffer_t framebuffer = g_init_boot_info->framebuffers[i];
        if(framebuffer.vaddr != nullptr) {
            struct flanterm_context* ft_ctx = flanterm_fb_init(
                log_fb_alloc,
                log_fb_free,
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

            // @todo: this sucks, we don't have heap yet so we just need to waste a page...
            framebuffer_t* framebuffer = (framebuffer_t*) (pmm_alloc_page(PMM_FLAG_PANIC) + g_init_boot_info->hhdm_offset);
            framebuffer->ft_ctx = ft_ctx;
            list_push_back(&g_framebuffer_list, &framebuffer->node);
        }
    }

    log_sink_t sink = { .min_level = LOG_LEVEL_INFO, .write = framebuffer_sink, .write_str = framebuffer_sink_str, .flush = framebuffer_sink_flush, .ctx = nullptr };
    if(log_add_sink(&sink)) {
        LOG_OKAY("Framebuffer logging initialized\n");
    } else {
        LOG_FAIL("Failed to add framebuffer log sink\n");
    }
}
