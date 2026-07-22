#include <common/arch.h>
#include <common/assert.h>
#include <common/cpu_local.h>
#include <common/hardware/pci.h>
#include <common/init.h>
#include <common/interrupts/interrupt.h>
#include <common/sync/mutex.h>
#include <common/time/time.h>

#if defined(__ARCH_X86_64__)
#include <arch/x86_64/cpu_local.h>
#include <arch/x86_64/hardware/ioapic.h>
#include <arch/x86_64/interrupts/interrupt.h>
#include <arch/x86_64/interrupts/interrupt_alloc.h>
#include <arch/x86_64/io.h>
#endif

#include <common/log.h>
#include <common/sync/spinlock.h>
#include <lib/helpers.h>
#include <lib/types.h>
#include <memory/heap.h>
#include <memory/vm.h>
#include <uacpi/kernel_api.h>
#include <uacpi/log.h>
#include <uacpi/platform/arch_helpers.h>
#include <uacpi/status.h>

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr* out_rsdp_address) {
    if(!g_init_boot_info->rdsp_physical) { return UACPI_STATUS_NOT_FOUND; }
    *out_rsdp_address = g_init_boot_info->rdsp_physical;
    return UACPI_STATUS_OK;
}

void* uacpi_kernel_map(uacpi_phys_addr paddr, uacpi_size length) {
    LOG_STRC("uacpi: mapping addr=%p, length=%zu\n", (void*) paddr, length);
    const uacpi_phys_addr aligned_paddr = ALIGN_DOWN(paddr, PAGE_SIZE_DEFAULT);
    const uacpi_size alignment_diff = paddr - aligned_paddr;
    const uacpi_size aligned_length = ALIGN_UP(length + alignment_diff, PAGE_SIZE_DEFAULT);

    virt_addr_t vaddr = (virt_addr_t) vm_map_direct(g_vm_global_address_space, VM_NO_HINT, aligned_length, VM_PROT_RW, VM_CACHE_NORMAL, aligned_paddr, VM_FLAG_NONE);

    return (void*) (vaddr + alignment_diff);
}

void uacpi_kernel_unmap(void* addr, uacpi_size length) {
    LOG_STRC("uacpi: unmapping addr=%p, length=%zu\n", addr, length);
    const uintptr_t aligned_addr = ALIGN_DOWN(addr, PAGE_SIZE_DEFAULT);
    const uacpi_size alignment_diff = (uintptr_t) addr - aligned_addr;
    const uacpi_size aligned_length = ALIGN_UP(length + alignment_diff, PAGE_SIZE_DEFAULT);

    vm_unmap(g_vm_global_address_space, (void*) aligned_addr, aligned_length);
}

static void uacpi_kernel_vlog(uacpi_log_level level, const uacpi_char* fmt, uacpi_va_list args) {
    log_level_t log_level;
    switch(level) {
        case UACPI_LOG_ERROR: log_level = LOG_LEVEL_FAIL; break;
        case UACPI_LOG_WARN:  log_level = LOG_LEVEL_WARN; break;
        case UACPI_LOG_DEBUG: log_level = LOG_LEVEL_DBGL; break;
        case UACPI_LOG_TRACE: log_level = LOG_LEVEL_STRC; break;
        case UACPI_LOG_INFO:  log_level = LOG_LEVEL_INFO; break;
    }


    switch(level) {
        case UACPI_LOG_ERROR: log_print(log_level, LOG_COLORIZE("fail |", "91") " uacpi: "); break;
        case UACPI_LOG_WARN:  log_print(log_level, LOG_COLORIZE("warn |", "93") " uacpi: "); break;
        case UACPI_LOG_DEBUG: log_print(log_level, LOG_COLORIZE("dbgl |", "34") " uacpi: "); break;
        case UACPI_LOG_TRACE: log_print(log_level, LOG_COLORIZE("strc |", "95") " uacpi: "); break;
        case UACPI_LOG_INFO:  log_print(log_level, LOG_COLORIZE("info |", "96") " uacpi: "); break;
    }

    log_vprint(log_level, fmt, args);
}

UACPI_PRINTF_DECL(2, 3)
void uacpi_kernel_log(uacpi_log_level level, const uacpi_char* fmt, ...) {
    va_list val;
    va_start(val, fmt);
    uacpi_kernel_vlog(level, fmt, val);
    va_end(val);
}


uacpi_status uacpi_kernel_pci_device_open(uacpi_pci_address address, uacpi_handle* out_handle) {
    LOG_INFO("uacpi: opening pci device at %04x:%02x:%02x:%02x\n", address.segment, address.bus, address.device, address.function);

    pci_device_handle_t* handle = (pci_device_handle_t*) heap_alloc(sizeof(pci_device_handle_t));
    handle->segment = address.segment;
    handle->bus = address.bus;
    handle->device = address.device;
    handle->function = address.function;
    *out_handle = (uacpi_handle) handle;
    return UACPI_STATUS_OK;
}

void uacpi_kernel_pci_device_close(uacpi_handle handle) {
    pci_device_handle_t* pci_handle = (pci_device_handle_t*) handle;
    heap_free(pci_handle, sizeof(pci_device_handle_t));
}

uacpi_status uacpi_kernel_pci_read8(uacpi_handle device, uacpi_size offset, uacpi_u8* value) {
    pci_device_handle_t* pci_handle = (pci_device_handle_t*) device;
    *value = pci_arch_read8(pci_handle, offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read16(uacpi_handle device, uacpi_size offset, uacpi_u16* value) {
    pci_device_handle_t* pci_handle = (pci_device_handle_t*) device;
    *value = pci_arch_read16(pci_handle, offset);
    return UACPI_STATUS_OK;
}
uacpi_status uacpi_kernel_pci_read32(uacpi_handle device, uacpi_size offset, uacpi_u32* value) {
    pci_device_handle_t* pci_handle = (pci_device_handle_t*) device;
    *value = pci_arch_read32(pci_handle, offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write8(uacpi_handle device, uacpi_size offset, uacpi_u8 value) {
    pci_device_handle_t* pci_handle = (pci_device_handle_t*) device;
    pci_arch_write8(pci_handle, offset, value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write16(uacpi_handle device, uacpi_size offset, uacpi_u16 value) {
    pci_device_handle_t* pci_handle = (pci_device_handle_t*) device;
    pci_arch_write16(pci_handle, offset, value);
    return UACPI_STATUS_OK;
}
uacpi_status uacpi_kernel_pci_write32(uacpi_handle device, uacpi_size offset, uacpi_u32 value) {
    pci_device_handle_t* pci_handle = (pci_device_handle_t*) device;
    pci_arch_write32(pci_handle, offset, value);
    return UACPI_STATUS_OK;
}

typedef struct {
    uacpi_io_addr base;
    uacpi_size len;
} kernel_io_handle_t;

uacpi_status uacpi_kernel_io_map(uacpi_io_addr base, uacpi_size len, uacpi_handle* out_handle) {
    LOG_STRC("uacpi: mapping io port base=0x%04lx, len=%zu\n", base, len);
    kernel_io_handle_t* handle = (kernel_io_handle_t*) heap_alloc(sizeof(kernel_io_handle_t));
    handle->base = base;
    handle->len = len;
    *out_handle = (uacpi_handle) handle;
    return UACPI_STATUS_OK;
}

void uacpi_kernel_io_unmap(uacpi_handle handle) {
    LOG_STRC("uacpi: unmapping io port handle=%p\n", handle);
    kernel_io_handle_t* io_handle = (kernel_io_handle_t*) handle;
    heap_free(io_handle, sizeof(kernel_io_handle_t));
}

#if defined(__ARCH_X86_64__)
uacpi_status uacpi_kernel_io_read8(uacpi_handle handle, uacpi_size offset, uacpi_u8* out_value) {
    kernel_io_handle_t* io_handle = (kernel_io_handle_t*) handle;
    LOG_STRC("uacpi: reading 8-bit value from io port 0x%04lx\n", io_handle->base + offset);
    assert(offset <= io_handle->len);
    *out_value = arch_io_port_read_u8(io_handle->base + offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read16(uacpi_handle handle, uacpi_size offset, uacpi_u16* out_value) {
    kernel_io_handle_t* io_handle = (kernel_io_handle_t*) handle;
    LOG_STRC("uacpi: reading 16-bit value from io port 0x%04lx\n", io_handle->base + offset);
    assert(offset + 1 <= io_handle->len);
    *out_value = arch_io_port_read_u16(io_handle->base + offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read32(uacpi_handle handle, uacpi_size offset, uacpi_u32* out_value) {
    kernel_io_handle_t* io_handle = (kernel_io_handle_t*) handle;
    LOG_STRC("uacpi: reading 32-bit value from io port 0x%04lx\n", io_handle->base + offset);
    assert(offset + 3 <= io_handle->len);
    *out_value = arch_io_port_read_u32(io_handle->base + offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write8(uacpi_handle handle, uacpi_size offset, uacpi_u8 in_value) {
    kernel_io_handle_t* io_handle = (kernel_io_handle_t*) handle;
    LOG_STRC("uacpi: write 8-bit value to io port 0x%04lx = 0x%02x\n", io_handle->base + offset, in_value);
    assert(offset <= io_handle->len);
    arch_io_port_write_u8(io_handle->base + offset, in_value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write16(uacpi_handle handle, uacpi_size offset, uacpi_u16 in_value) {
    kernel_io_handle_t* io_handle = (kernel_io_handle_t*) handle;
    LOG_STRC("uacpi: write 16-bit value to io port 0x%04lx = 0x%04x\n", io_handle->base + offset, in_value);
    assert(offset + 1 <= io_handle->len);
    arch_io_port_write_u16(io_handle->base + offset, in_value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write32(uacpi_handle handle, uacpi_size offset, uacpi_u32 in_value) {
    kernel_io_handle_t* io_handle = (kernel_io_handle_t*) handle;
    LOG_STRC("uacpi: write 32-bit value to io port 0x%04lx = 0x%08x\n", io_handle->base + offset, in_value);
    assert(offset + 3 <= io_handle->len);
    arch_io_port_write_u32(io_handle->base + offset, in_value);
    return UACPI_STATUS_OK;
}
#elif defined(__ARCH_RISCV64__)
uacpi_status uacpi_kernel_io_read8(uacpi_handle handle, uacpi_size offset, uacpi_u8* out_value) {
    (void) handle;
    (void) offset;
    (void) out_value;
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_io_read16(uacpi_handle handle, uacpi_size offset, uacpi_u16* out_value) {
    (void) handle;
    (void) offset;
    (void) out_value;
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_io_read32(uacpi_handle handle, uacpi_size offset, uacpi_u32* out_value) {
    (void) handle;
    (void) offset;
    (void) out_value;
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_io_write8(uacpi_handle handle, uacpi_size offset, uacpi_u8 in_value) {
    (void) handle;
    (void) offset;
    (void) in_value;
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_io_write16(uacpi_handle handle, uacpi_size offset, uacpi_u16 in_value) {
    (void) handle;
    (void) offset;
    (void) in_value;
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_io_write32(uacpi_handle handle, uacpi_size offset, uacpi_u32 in_value) {
    (void) handle;
    (void) offset;
    (void) in_value;
    return UACPI_STATUS_UNIMPLEMENTED;
}
#endif

void* uacpi_kernel_alloc(uacpi_size size) {
    return heap_alloc(size);
}

void uacpi_kernel_free(void* mem, uacpi_size size_hint) {
    heap_free(mem, size_hint);
}

uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot(void) {
    return time_monotonic_ns();
}

/**
 * Spin for N microseconds.
 */
[[noreturn]] void uacpi_kernel_stall(uacpi_u8 usec) {
    (void) usec;
    assert(false);
}

/**
 * Sleep for N milliseconds.
 */
[[noreturn]] void uacpi_kernel_sleep(uacpi_u64 msec) {
    (void) msec;
    assert(false);
}

/**
 * Create/free an opaque non-recursive kernel mutex object.
 */
uacpi_handle uacpi_kernel_create_mutex(void) {
    mutex_t* mutex = (mutex_t*) heap_alloc(sizeof(mutex_t));
    *mutex = MUTEX_INIT;
    return (uacpi_handle) mutex;
}

void uacpi_kernel_free_mutex(uacpi_handle handle) {
    mutex_t* mutex = (mutex_t*) handle;
    heap_free(mutex, sizeof(mutex_t));
}

typedef struct {
    uacpi_u32 counter;
} kernel_event_t;

/**
 * Create/free an opaque kernel (semaphore-like) event object.
 */
uacpi_handle uacpi_kernel_create_event(void) {
    return (uacpi_handle) heap_alloc(sizeof(kernel_event_t));
}

void uacpi_kernel_free_event(uacpi_handle handle) {
    heap_free((kernel_event_t*) handle, sizeof(kernel_event_t));
}

/**
 * Try to wait for an event (counter > 0) with a millisecond timeout.
 * A timeout value of 0xFFFF implies infinite wait.
 *
 * The internal counter is decremented by 1 if wait was successful.
 *
 * A successful wait is indicated by returning UACPI_TRUE.
 */
uacpi_bool uacpi_kernel_wait_for_event(uacpi_handle handle, uacpi_u16 timeout) {
    assert(timeout == 0xffff);
    kernel_event_t* event = (kernel_event_t*) handle;
    while(ATOMIC_LOAD(&event->counter, ATOMIC_RELAXED) == 0) { arch_spin_hint(); }
    return UACPI_TRUE;
}

/**
 * Signal the event object by incrementing its internal counter by 1.
 *
 * This function may be used in interrupt contexts.
 */
void uacpi_kernel_signal_event(uacpi_handle handle) {
    kernel_event_t* event = (kernel_event_t*) handle;
    ATOMIC_LOAD_ADD(&event->counter, 1, ATOMIC_RELEASE);
}

/**
 * Reset the event counter to 0.
 */
void uacpi_kernel_reset_event(uacpi_handle handle) {
    kernel_event_t* event = (kernel_event_t*) handle;
    ATOMIC_STORE(&event->counter, 0, ATOMIC_RELEASE);
}

/**
 * Returns a unique identifier of the currently executing thread.
 *
 * The returned thread id cannot be UACPI_THREAD_ID_NONE.
 */
uacpi_thread_id uacpi_kernel_get_thread_id(void) {
    return (void*) (uintptr_t) CPU_LOCAL_GET_CURRENT_THREAD()->common.tid;
}

/**
 * Disable interrupts and return a kernel-defined value representing the
 * "before" state. This value is used in the subsequent call to restore the
 * prior state.
 *
 * Note that this is talking about ALL interrupts on the current CPU, not just
 * those installed by uACPI. This is typically achieved by executing the 'cli'
 * instruction on x86, 'msr daifset, #3' on aarch64 etc.
 */
uacpi_interrupt_state uacpi_kernel_disable_interrupts(void) {
    assert(false);
}

/**
 * Restore the state of the interrupt flags to the kernel-defined value provided
 * in 'state'.
 */
[[noreturn]] void uacpi_kernel_restore_interrupts(uacpi_interrupt_state) {
    assert(false);
}

/**
 * Try to acquire the mutex with a millisecond timeout.
 *
 * The timeout value has the following meanings:
 * 0x0000 - Attempt to acquire the mutex once, in a non-blocking manner
 * 0x0001...0xFFFE - Attempt to acquire the mutex for at least 'timeout'
 *                   milliseconds
 * 0xFFFF - Infinite wait, block until the mutex is acquired
 *
 * The following are possible return values:
 * 1. UACPI_STATUS_OK - successful acquire operation
 * 2. UACPI_STATUS_TIMEOUT - timeout reached while attempting to acquire (or the
 *                           single attempt to acquire was not successful for
 *                           calls with timeout=0)
 * 3. Any other value - signifies a host internal error and is treated as such
 */
uacpi_status uacpi_kernel_acquire_mutex(uacpi_handle handle, uacpi_u16 timeout) {
    assert(timeout == 0xffff);
    mutex_t* spinlock = (mutex_t*) handle;
    mutex_acquire(spinlock);
    return UACPI_STATUS_OK;
}

void uacpi_kernel_release_mutex(uacpi_handle handle) {
    mutex_t* spinlock = (mutex_t*) handle;
    mutex_release(spinlock);
}

/**
 * Handle a firmware request.
 *
 * Currently either a Breakpoint or Fatal operators.
 */
uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request*) {
    assert(false);
}

/**
 * Install an interrupt handler at 'irq', 'ctx' is passed to the provided
 * handler for every invocation.
 *
 * 'out_irq_handle' is set to a kernel-implemented value that can be used to
 * refer to this handler from other API.
 */

typedef struct {
    uint32_t vector;
    uacpi_interrupt_handler handler;
    uacpi_handle ctx;
} uacpi_kernel_interrupt_ctx_t;

#if defined(__ARCH_X86_64__)
static void uacpi_kernel_interrupt_handler(arch_interrupt_frame_t* frame, void* p_ctx) {
    (void) frame;
    uacpi_kernel_interrupt_ctx_t* ctx = (uacpi_kernel_interrupt_ctx_t*) p_ctx;
    uacpi_interrupt_ret ret = ctx->handler(ctx->ctx);
    if(ret == UACPI_INTERRUPT_NOT_HANDLED) { arch_panic("[uacpi] Handled interrupt\n"); }
}
#endif

uacpi_status uacpi_kernel_install_interrupt_handler(uacpi_u32 irq, uacpi_interrupt_handler handler, uacpi_handle ctx, uacpi_handle* out_irq_handle) {
#if defined(__ARCH_X86_64__)
    uacpi_kernel_interrupt_ctx_t* kernel_ctx = (uacpi_kernel_interrupt_ctx_t*) heap_alloc(sizeof(uacpi_kernel_interrupt_ctx_t));

    uint8_t vector = arch_interrupt_alloc_allocate();
    LOG_STRC("uacpi: installing interrupt handler for irq=%u -> %u\n", irq, vector);

    kernel_ctx->vector = vector;
    kernel_ctx->handler = handler;
    kernel_ctx->ctx = ctx;

    // @todo: every interrupt will be routed to the bsp :/
    arch_ioapic_map_legacy_irq(irq, CPU_LOCAL_READ(lapic_id), false, true, vector);
    interrupt_set_handler(vector, uacpi_kernel_interrupt_handler, kernel_ctx);
    *out_irq_handle = (uacpi_handle) kernel_ctx;
    return UACPI_STATUS_OK;
#elif defined(__ARCH_RISCV64__)
    (void) irq;
    (void) handler;
    (void) ctx;
    (void) out_irq_handle;
    assert(false && "Unimplemented");
    return UACPI_STATUS_UNIMPLEMENTED;
#endif
}

/**
 * Uninstall an interrupt handler. 'irq_handle' is the value returned via
 * 'out_irq_handle' during installation.
 */
uacpi_status uacpi_kernel_uninstall_interrupt_handler(uacpi_interrupt_handler, uacpi_handle irq_handle) {
    (void) irq_handle;
    assert(false);
}

/**
 * Create/free a kernel spinlock object.
 *
 * Unlike other types of locks, spinlocks may be used in interrupt contexts.
 */
uacpi_handle uacpi_kernel_create_spinlock(void) {
    spinlock_no_int_t* spinlock = (spinlock_no_int_t*) heap_alloc(sizeof(spinlock_no_int_t));
    *spinlock = SPINLOCK_NO_INT_INIT;
    return (uacpi_handle) spinlock;
}

void uacpi_kernel_free_spinlock(uacpi_handle handle) {
    spinlock_no_int_t* spinlock = (spinlock_no_int_t*) handle;
    heap_free(spinlock, sizeof(spinlock_no_int_t));
}

/**
 * Lock/unlock helpers for spinlocks.
 *
 * These are expected to disable interrupts, returning the previous state of cpu
 * flags, that can be used to possibly re-enable interrupts if they were enabled
 * before.
 *
 * Note that lock is infalliable.
 */
uacpi_cpu_flags uacpi_kernel_lock_spinlock(uacpi_handle handle) {
    spinlock_no_int_t* spinlock = (spinlock_no_int_t*) handle;
    // @todo: figure out a better way to do this
    uacpi_cpu_flags flags = (uacpi_cpu_flags) spinlock_noint_lock(spinlock).enabled;
    return flags;
}

void uacpi_kernel_unlock_spinlock(uacpi_handle handle, uacpi_cpu_flags flags) {
    spinlock_no_int_t* spinlock = (spinlock_no_int_t*) handle;
    // @todo: figure out a better way to do this
    spinlock_noint_unlock(spinlock, (arch_interrupt_state_t) { .enabled = (bool) flags });
}

uacpi_status uacpi_kernel_schedule_work(uacpi_work_type, uacpi_work_handler, uacpi_handle ctx) {
    (void) ctx;
    assert(false);
}

/**
 * Waits for two types of work to finish:
 * 1. All in-flight interrupts installed via uacpi_kernel_install_interrupt_handler
 * 2. All work scheduled via uacpi_kernel_schedule_work
 *
 * Note that the waits must be done in this order specifically.
 */
uacpi_status uacpi_kernel_wait_for_work_completion(void) {
    assert(false);
}
