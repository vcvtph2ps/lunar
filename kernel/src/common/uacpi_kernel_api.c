#include <common/arch.h>
#include <common/assert.h>
#include <common/cpu_local.h>
#include <common/hardware/pci.h>
#include <common/init.h>
#include <common/interrupts/dpc.h>
#include <common/interrupts/interrupt.h>
#include <common/log.h>
#include <common/sched/sched.h>
#include <common/sync/lock.h>
#include <common/sync/spinlock.h>
#include <common/sync/wait_obj.h>
#include <common/time/time.h>
#include <lib/helpers.h>
#include <lib/list.h>
#include <lib/types.h>
#include <memory/heap.h>
#include <memory/pagedb.h>
#include <memory/ptm.h>
#include <memory/vm.h>
#include <stdint.h>
#include <uacpi/kernel_api.h>
#include <uacpi/log.h>
#include <uacpi/platform/arch_helpers.h>
#include <uacpi/status.h>

#if defined(__x86_64__)
#include <arch/x86_64/hardware/ioapic.h>
#include <arch/x86_64/interrupts/interrupt_alloc.h>
#include <arch/x86_64/io.h>
#elif defined(__riscv) && __riscv_xlen == 64
#include <arch/riscv64/io.h>
#endif

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr* out_rsdp_address) {
    if(!g_init_boot_info->rdsp_physical) {
        return UACPI_STATUS_NOT_FOUND;
    }
    *out_rsdp_address = g_init_boot_info->rdsp_physical;
    return UACPI_STATUS_OK;
}

static bool uacpi_phys_is_ram(uintptr_t paddr, size_t length) {
    if(length == 0 || length > UINTPTR_MAX - paddr) {
        return false;
    }

    const uintptr_t end = paddr + length;
    const uint64_t last_pfn = (end - 1) / PAGE_SIZE_DEFAULT;
    for(uint64_t pfn = paddr / PAGE_SIZE_DEFAULT; pfn <= last_pfn; pfn++) {
        if(!pagedb_valid_page(pfn)) {
            return false;
        }
    }

    for(size_t i = 0; i < g_init_boot_info->mm_entry_count; i++) {
        const bootinfo_mm_entry_t* entry = &g_init_boot_info->mm_entries[i];
        if(paddr < entry->phys_base) {
            return false;
        }
        const uintptr_t offset = paddr - entry->phys_base;
        if(offset >= entry->length) {
            continue;
        }

        switch(entry->type) {
            case BOOTINFO_MM_TYPE_USABLE:
            case BOOTINFO_MM_TYPE_USED:
            case BOOTINFO_MM_TYPE_MODULE:
            case BOOTINFO_MM_TYPE_RECLAIMABLE:
            case BOOTINFO_MM_TYPE_ACPI_RECLAIMABLE: break;
            default:                                return false;
        }

        const size_t remaining = entry->length - offset;
        if(remaining >= end - paddr) {
            return true;
        }
        paddr += remaining;
    }
    return false;
}

void* uacpi_kernel_map(uacpi_phys_addr paddr, uacpi_size length) {
    LOG_KTRC("uacpi: mapping addr=%p, length=%zu\n", (void*) paddr, length);
    const uacpi_phys_addr aligned_paddr = ALIGN_DOWN(paddr, PAGE_SIZE_DEFAULT);
    const uacpi_size alignment_diff = paddr - aligned_paddr;
    const uacpi_size aligned_length = ALIGN_UP(length + alignment_diff, PAGE_SIZE_DEFAULT);

    const bool mmio = !uacpi_phys_is_ram(aligned_paddr, aligned_length);
    if(!mmio) {
        return PTM_TO_HHDM(paddr);
    }

    virt_addr_t vaddr = (virt_addr_t) vm_map_direct(g_vm_global_address_space, VM_NO_HINT, aligned_length, VM_PROT_RW, VM_CACHE_DISABLE, aligned_paddr, VM_FLAG_NO_PAGEDB);
    if(vaddr == 0) {
        LOG_FAIL("uacpi: failed to map physical address %p, length=%zu\n", (void*) paddr, length);
        return nullptr;
    }

    return (void*) (vaddr + alignment_diff);
}

void uacpi_kernel_unmap(void* addr, uacpi_size length) {
    LOG_KTRC("uacpi: unmapping addr=%p, length=%zu\n", addr, length);
    const uintptr_t aligned_addr = ALIGN_DOWN(addr, PAGE_SIZE_DEFAULT);
    const uacpi_size alignment_diff = (uintptr_t) addr - aligned_addr;
    const uacpi_size aligned_length = ALIGN_UP(length + alignment_diff, PAGE_SIZE_DEFAULT);

    if(aligned_addr >= g_init_boot_info->hhdm_offset && aligned_addr + length < g_init_boot_info->hhdm_offset + g_init_boot_info->hhdm_size) {
        /// region is in the hhdm. no-op
        return;
    }

    vm_unmap(g_vm_global_address_space, (void*) aligned_addr, aligned_length);
}

static void uacpi_kernel_vlog(uacpi_log_level level, const uacpi_char* fmt, uacpi_va_list args) {
    log_level_t log_level;
    const char* prefix;
    switch(level) {
        case UACPI_LOG_ERROR:
            log_level = LOG_LEVEL_FAIL;
            prefix = LOG_COLORIZE("fail |", "9") " uacpi: ";
            break;
        case UACPI_LOG_WARN:
            log_level = LOG_LEVEL_WARN;
            prefix = LOG_COLORIZE("warn |", "11") " uacpi: ";
            break;
        case UACPI_LOG_DEBUG:
            log_level = LOG_LEVEL_DBGL;
            prefix = LOG_COLORIZE("dbgl |", "4") " uacpi: ";
            break;
        case UACPI_LOG_TRACE:
            log_level = LOG_LEVEL_STRC;
            prefix = LOG_COLORIZE("strc |", "13") " uacpi: ";
            break;
        case UACPI_LOG_INFO:
            log_level = LOG_LEVEL_INFO;
            prefix = LOG_COLORIZE("info |", "14") " uacpi: ";
            break;
        default: return;
    }

    arch_interrupt_state_t saved = log_lock_acquire();
    log_print_lockless(log_level, "%s", prefix);
    log_vprint_lockless(log_level, fmt, args);
    log_lock_release(saved);
}

UACPI_PRINTF_DECL(2, 3)
void uacpi_kernel_log(uacpi_log_level level, const uacpi_char* fmt, ...) {
    va_list val;
    va_start(val, fmt);
    uacpi_kernel_vlog(level, fmt, val);
    va_end(val);
}

extern bool g_pci_acpi_initialized;

typedef struct {
    bool allocated;
    pci_device_access_t* access;
} pci_handle_t;

uacpi_status uacpi_kernel_pci_device_open(uacpi_pci_address address, uacpi_handle* out_handle) {
    LOG_INFO("uacpi: opening pci device at %04x:%02x:%02x:%02x\n", address.segment, address.bus, address.device, address.function);

    pci_handle_t* handle = (pci_handle_t*) heap_alloc(sizeof(pci_handle_t));

    if(!g_pci_acpi_initialized) {
        pci_device_access_t* access = (pci_device_access_t*) heap_alloc(sizeof(pci_device_access_t));

        assert(address.segment == 0);
        access->segment = address.segment;
        access->bus = address.bus;
        access->device = address.device;
        access->function = address.function;
        access->ecam_window = nullptr;

        handle->allocated = true;
        handle->access = access;
        *out_handle = (uacpi_handle) handle;

        pci_ecam_region_t* ecam_base = pci_find_ecam_region(address.segment, address.bus);
        if(ecam_base == nullptr && address.segment != 0) {
            arch_panic("uacpi: failed to find ecam region for segment %04x bus %02x\n", address.segment, address.bus);
        } else if(ecam_base == nullptr) {
            return UACPI_STATUS_OK;
        }
        access->ecam_window = (uint32_t*) (ecam_base->mmio_base + pci_ecam_offset(address.bus, address.device, address.function));
    } else {
        pci_device_t* device;
        if(!pci_device_find(address.segment, address.bus, address.device, address.function, &device)) {
            LOG_FAIL("uacpi: failed to find pci device at %04x:%02x:%02x:%02x\n", address.segment, address.bus, address.device, address.function);
            heap_free(handle, sizeof(pci_handle_t));
            return UACPI_STATUS_NOT_FOUND;
        }

        handle->allocated = false;
        handle->access = &device->access;
        *out_handle = (uacpi_handle) handle;
    }

    return UACPI_STATUS_OK;
}

void uacpi_kernel_pci_device_close(uacpi_handle handle) {
    pci_handle_t* pci_handle = (pci_handle_t*) handle;
    if(pci_handle->allocated) {
        heap_free(pci_handle->access, sizeof(pci_device_access_t));
    }
    heap_free(pci_handle, sizeof(pci_handle_t));
}

uacpi_status uacpi_kernel_pci_read8(uacpi_handle device, uacpi_size offset, uacpi_u8* value) {
    pci_handle_t* pci_access = (pci_handle_t*) device;
    *value = pci_device_read_u8(pci_access->access, offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read16(uacpi_handle device, uacpi_size offset, uacpi_u16* value) {
    pci_handle_t* pci_access = (pci_handle_t*) device;
    *value = pci_device_read_u16(pci_access->access, offset);
    return UACPI_STATUS_OK;
}
uacpi_status uacpi_kernel_pci_read32(uacpi_handle device, uacpi_size offset, uacpi_u32* value) {
    pci_handle_t* pci_access = (pci_handle_t*) device;
    *value = pci_device_read_u32(pci_access->access, offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write8(uacpi_handle device, uacpi_size offset, uacpi_u8 value) {
    pci_handle_t* pci_access = (pci_handle_t*) device;
    pci_device_write_u8(pci_access->access, offset, value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write16(uacpi_handle device, uacpi_size offset, uacpi_u16 value) {
    pci_handle_t* pci_access = (pci_handle_t*) device;
    pci_device_write_u16(pci_access->access, offset, value);
    return UACPI_STATUS_OK;
}
uacpi_status uacpi_kernel_pci_write32(uacpi_handle device, uacpi_size offset, uacpi_u32 value) {
    pci_handle_t* pci_access = (pci_handle_t*) device;
    pci_device_write_u32(pci_access->access, offset, value);
    return UACPI_STATUS_OK;
}

uacpi_u8 uacpi_kernel_mmio_read8(void* addr) {
    return arch_io_mem_read_u8((uintptr_t) addr);
}
uacpi_u16 uacpi_kernel_mmio_read16(void* addr) {
    return arch_io_mem_read_u16((uintptr_t) addr);
}
uacpi_u32 uacpi_kernel_mmio_read32(void* addr) {
    return arch_io_mem_read_u32((uintptr_t) addr);
}
uacpi_u64 uacpi_kernel_mmio_read64(void* addr) {
    return arch_io_mem_read_u64((uintptr_t) addr);
}

void uacpi_kernel_mmio_write8(void* addr, uacpi_u8 value) {
    return arch_io_mem_write_u8((uintptr_t) addr, value);
}
void uacpi_kernel_mmio_write16(void* addr, uacpi_u16 value) {
    return arch_io_mem_write_u16((uintptr_t) addr, value);
}
void uacpi_kernel_mmio_write32(void* addr, uacpi_u32 value) {
    return arch_io_mem_write_u32((uintptr_t) addr, value);
}
void uacpi_kernel_mmio_write64(void* addr, uacpi_u64 value) {
    return arch_io_mem_write_u64((uintptr_t) addr, value);
}

uacpi_status uacpi_kernel_io_map(uacpi_io_addr base, uacpi_size len, uacpi_handle* out_handle) {
    (void) base;
    (void) len;
    LOG_KTRC("uacpi: mapping io port base=0x%04lx, len=%zu\n", base, len);

    *out_handle = (uacpi_handle) base;
    return UACPI_STATUS_OK;
}

void uacpi_kernel_io_unmap(uacpi_handle handle) {
    (void) handle;
    LOG_KTRC("uacpi: unmapping io port handle=%p\n", handle);
}

#if defined(__x86_64__)
uacpi_status uacpi_kernel_io_read8(uacpi_handle handle, uacpi_size offset, uacpi_u8* out_value) {
    uacpi_io_addr io_base = (uacpi_io_addr) handle;
    LOG_KTRC("uacpi: reading 8-bit value from io port 0x%04lx\n", io_base + offset);
    *out_value = arch_io_port_read_u8(io_base + offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read16(uacpi_handle handle, uacpi_size offset, uacpi_u16* out_value) {
    uacpi_io_addr io_base = (uacpi_io_addr) handle;
    LOG_KTRC("uacpi: reading 16-bit value from io port 0x%04lx\n", io_base + offset);
    *out_value = arch_io_port_read_u16(io_base + offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read32(uacpi_handle handle, uacpi_size offset, uacpi_u32* out_value) {
    uacpi_io_addr io_base = (uacpi_io_addr) handle;
    LOG_KTRC("uacpi: reading 32-bit value from io port 0x%04lx\n", io_base + offset);
    *out_value = arch_io_port_read_u32(io_base + offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write8(uacpi_handle handle, uacpi_size offset, uacpi_u8 in_value) {
    uacpi_io_addr io_base = (uacpi_io_addr) handle;
    LOG_KTRC("uacpi: write 8-bit value to io port 0x%04lx = 0x%02x\n", io_base + offset, in_value);
    arch_io_port_write_u8(io_base + offset, in_value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write16(uacpi_handle handle, uacpi_size offset, uacpi_u16 in_value) {
    uacpi_io_addr io_base = (uacpi_io_addr) handle;
    LOG_KTRC("uacpi: write 16-bit value to io port 0x%04lx = 0x%04x\n", io_base + offset, in_value);
    arch_io_port_write_u16(io_base + offset, in_value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write32(uacpi_handle handle, uacpi_size offset, uacpi_u32 in_value) {
    uacpi_io_addr io_base = (uacpi_io_addr) handle;
    LOG_KTRC("uacpi: write 32-bit value to io port 0x%04lx = 0x%08x\n", io_base + offset, in_value);
    arch_io_port_write_u32(io_base + offset, in_value);
    return UACPI_STATUS_OK;
}
#elif defined(__riscv) && __riscv_xlen == 64
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

void* uacpi_kernel_alloc_zeroed(uacpi_size size) {
    return heap_zalloc(size);
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
void uacpi_kernel_stall(uacpi_u8 usec) {
    uint64_t start = time_monotonic_ns();
    uint64_t delta = usec * 1000ULL;
    while((time_monotonic_ns() - start) < delta) {
        arch_spin_hint();
    }
}

/**
 * Sleep for N milliseconds.
 */
void uacpi_kernel_sleep(uacpi_u64 msec) {
    if(msec == 0) {
        return;
    }
    if(CPU_LOCAL_READ(scheduler.preempt_counter) > 0) {
        LOG_WARN("uacpi: sleep called with preemption disabled, busy-waiting for %lu ms\n", msec);
        uacpi_kernel_stall(msec * 1000);
        return;
    }
    sched_sleep(msec);
}

typedef struct {
    uacpi_u32 counter;
} kernel_event_t;

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
    arch_interrupt_state_t state = arch_interrupt_disable();
    return (uacpi_interrupt_state) state.enabled;
}

/**
 * Restore the state of the interrupt flags to the kernel-defined value provided
 * in 'state'.
 */
void uacpi_kernel_restore_interrupts(uacpi_interrupt_state interrupt_state) {
    arch_interrupt_state_t state = (arch_interrupt_state_t) { .enabled = (bool) interrupt_state };
    arch_interrupt_restore(state);
}


/**
 * Handle a firmware request.
 *
 * Currently either a Breakpoint or Fatal operators.
 */
uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request*) {
    assert(false);
}

ATOMIC static uint32_t g_uacpi_interrupt_count = 0; // we use this instead of the list count so we can poll it without disabling interrupts
ATOMIC static uint32_t g_uacpi_work_count = 0; // we use this instead of the list count so we can poll it without disabling interrupts

/**
 * Install an interrupt handler at 'irq', 'ctx' is passed to the provided
 * handler for every invocation.
 *
 * 'out_irq_handle' is set to a kernel-implemented value that can be used to
 * refer to this handler from other API.
 */
typedef struct {
    uint32_t vector;
    uint8_t gsi;
    uacpi_interrupt_handler handler;
    uacpi_handle ctx;
    dpc_t* work_item;
} uacpi_kernel_interrupt_ctx_t;

#if defined(__x86_64__)
static void uacpi_kernel_interrupt_handler(arch_interrupt_frame_t* frame, void* p_ctx) {
    (void) frame;
    ATOMIC_LOAD_ADD(&g_uacpi_interrupt_count, 1, ATOMIC_ACQUIRE);
    uacpi_kernel_interrupt_ctx_t* ctx = (uacpi_kernel_interrupt_ctx_t*) p_ctx;
    arch_ioapic_mask_gsi(ctx->gsi, true);
    dpc_queue(ctx->work_item);
}

static void uacpi_kernel_interrupt_handler_deferred(void* p_ctx) {
    uacpi_kernel_interrupt_ctx_t* ctx = (uacpi_kernel_interrupt_ctx_t*) p_ctx;
    uacpi_interrupt_ret ret = ctx->handler(ctx->ctx);
    arch_ioapic_mask_gsi(ctx->gsi, false);
    if(ret == UACPI_INTERRUPT_NOT_HANDLED) {
        arch_panic("uacpi: Unhandled interrupt\n");
    }
    ATOMIC_LOAD_SUB(&g_uacpi_interrupt_count, 1, ATOMIC_ACQUIRE);
}
#endif


uacpi_status uacpi_kernel_install_interrupt_handler(uacpi_u32 irq, uacpi_interrupt_handler handler, uacpi_handle ctx, uacpi_handle* out_irq_handle) {
#if defined(__x86_64__)
    uacpi_kernel_interrupt_ctx_t* kernel_ctx = (uacpi_kernel_interrupt_ctx_t*) heap_alloc(sizeof(uacpi_kernel_interrupt_ctx_t));

    uint8_t vector = arch_interrupt_alloc_allocate();
    LOG_KTRC("uacpi: installing interrupt handler for irq=%u -> %u\n", irq, vector);

    kernel_ctx->vector = vector;
    kernel_ctx->gsi = arch_ioapic_gsi_of_irq(irq);
    kernel_ctx->handler = handler;
    kernel_ctx->ctx = ctx;
    kernel_ctx->work_item = dpc_create(uacpi_kernel_interrupt_handler_deferred, kernel_ctx);

    // @todo: every interrupt will be routed to the bsp :/
    arch_ioapic_map_legacy_irq(irq, CPU_LOCAL_READ(lapic_id), false, true, vector);
    interrupt_set_hardirq_handler(vector, uacpi_kernel_interrupt_handler, kernel_ctx);
    *out_irq_handle = (uacpi_handle) kernel_ctx;
    return UACPI_STATUS_OK;
#elif defined(__riscv) && __riscv_xlen == 64
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

typedef struct {
    uacpi_work_handler handler;
    uacpi_handle ctx;
    list_node_t node;
} uacpi_work_item_t;

static spinlock_no_int_t g_uacpi_work_list_lock;
static list_t g_uacpi_work_list;
static wait_obj_t g_uacpi_work_signal = WAIT_OBJ_INIT_SYNCHRONIZATION();

[[noreturn]] static void uacpi_work_thread() {
    while(1) {
        sched_wait_single(&g_uacpi_work_signal, UINT64_MAX);

        while(1) {
            arch_interrupt_state_t state = spinlock_noint_lock(&g_uacpi_work_list_lock);
            list_node_t* node = list_pop(&g_uacpi_work_list);
            spinlock_noint_unlock(&g_uacpi_work_list_lock, state);

            if(node == nullptr) {
                break;
            }

            uacpi_work_item_t* item = CONTAINER_OF(node, uacpi_work_item_t, node);
            item->handler(item->ctx);
            ATOMIC_LOAD_SUB(&g_uacpi_work_count, 1, ATOMIC_ACQUIRE);
        }
    }
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-prototypes"
void uacpi_create_kernel_thread() {
    thread_t* thread = sched_arch_create_kernel_thread((virt_addr_t) uacpi_work_thread);
    ATOMIC_STORE(&thread->sched.migratable, false, ATOMIC_SEQ_CST);
    sched_thread_schedule(thread);
}
#pragma clang diagnostic pop

/**
 * Waits for two types of work to finish:
 * 1. All in-flight interrupts installed via uacpi_kernel_install_interrupt_handler
 * 2. All work scheduled via uacpi_kernel_schedule_work
 *
 * Note that the waits must be done in this order specifically.
 */
uacpi_status uacpi_kernel_wait_for_work_completion(void) {
    while(ATOMIC_LOAD(&g_uacpi_interrupt_count, ATOMIC_RELAXED) != 0) {
        sched_sleep(25);
    }

    while(ATOMIC_LOAD(&g_uacpi_work_count, ATOMIC_RELAXED) != 0) {
        sched_sleep(25);
    }

    return UACPI_STATUS_OK;
}


/**
 * Give one unit to the semaphore, waking up one of the threads that are
 * waiting for it, if any.
 *
 * A semaphore has no notion of an owner: the unit may be given by a thread
 * other than the one that took it.
 *
 * This function may be used in interrupt contexts, as well as with interrupts
 * disabled and a spinlock held, and therefore must not sleep. Note that this
 * applies to every semaphore, including those that are used as locks.
 */
void uacpi_kernel_signal_semaphore(uacpi_handle handle) {
    wait_obj_signal((wait_obj_t*) handle, 1);
}
/**
 * Try to take one unit from the semaphore with a millisecond timeout.
 *
 * The timeout value has the following meanings:
 * 0x0000 - Attempt to take a unit once, in a non-blocking manner
 * 0x0001...0xFFFE - Attempt to take a unit for at least 'timeout' milliseconds
 * 0xFFFF - Infinite wait, block until a unit is available
 *
 * The following are possible return values:
 * 1. UACPI_STATUS_OK - a unit was taken
 * 2. UACPI_STATUS_TIMEOUT - timeout reached while waiting for a unit (or the
 *                           single attempt to take one was not successful for
 *                           calls with timeout=0)
 * 3. Any other value - signifies a host internal error and is treated as such
 *
 * This function is never used in interrupt contexts, unless that's where the
 * kernel chooses to execute deferred work, see uacpi_kernel_schedule_work.
 */
uacpi_status uacpi_kernel_wait_for_semaphore(uacpi_handle handle, uacpi_u16 timeout) {
    wait_obj_t* obj = (wait_obj_t*) handle;
    assert(timeout == 0xffff);
    sched_wait_single(obj, UINT64_MAX);
    return UACPI_STATUS_OK;
}

/**
 * Create/free an opaque kernel counting semaphore object that starts out with
 * 'initial_units' units available.
 *
 * This is the only blocking synchronization primitive that uACPI relies upon,
 * it's used for two things:
 * - As a lock, in which case it's created with 1 unit, and never has more
 *   than that.
 * - As an event, in which case it's created with 0 units, and there's no
 *   limit on how many of them it may accumulate. A kernel whose semaphore
 *   requires one should use the largest value that it supports.
 *
 * A semaphore is never freed while there's a thread waiting on it, as long as
 * uacpi_state_reset is not called with another thread still inside of uACPI.
 */
uacpi_handle uacpi_kernel_create_semaphore(uacpi_u32 initial_units) {
    size_t units = initial_units;
    if(units == 0) {
        units = SIZE_MAX;
    }

    wait_obj_t* wait_obj = (wait_obj_t*) heap_alloc(sizeof(wait_obj_t));
    *wait_obj = WAIT_OBJ_INIT_SEMAPHORE(units);
    if(units == 1) {
        ATOMIC_STORE(&wait_obj->signal_state, 1, ATOMIC_RELAXED);
    }
    return (uacpi_handle) wait_obj;
}

void uacpi_kernel_free_semaphore(uacpi_handle handle) {
    wait_obj_t* wait_obj = (wait_obj_t*) handle;
    heap_free(wait_obj, sizeof(wait_obj_t));
}

/**
 * Schedules 'handler' to be invoked with 'ctx' as its only argument.
 * Might be invoked from an interrupt context.
 *
 * 'work_item' is there for the kernel to keep track of this work with, it's
 * never scheduled while still pending, that is before the handler that it was
 * last scheduled with has been invoked.
 *
 * Once that happens the work item may be reused: the handler is allowed to
 * schedule it again, possibly as a different type of work, or to free it. The
 * kernel therefore must not access a work item after it has invoked the
 * handler, which means that both 'handler' and 'ctx' have to be taken out of
 * it, and any of its state updated, before the handler is invoked and not
 * after it returns.
 */
void uacpi_kernel_schedule_work(uacpi_work_type, uacpi_handle work_item, uacpi_work_handler handler, uacpi_handle ctx) {
    uacpi_work_item_t* item = work_item;
    item->handler = handler;
    item->ctx = ctx;

    ATOMIC_LOAD_ADD(&g_uacpi_work_count, 1, ATOMIC_ACQUIRE);
    arch_interrupt_state_t state = spinlock_noint_lock(&g_uacpi_work_list_lock);
    list_push_back(&g_uacpi_work_list, &item->node);
    spinlock_noint_unlock(&g_uacpi_work_list_lock, state);
    wait_obj_signal(&g_uacpi_work_signal, 0);
}

/**
 * Create/free an opaque kernel work item object, which is what a piece of
 * deferred work is scheduled with, see uacpi_kernel_schedule_work.
 *
 * A work item is always created ahead of the time that it's needed, so that
 * scheduling it doesn't require any memory to be allocated. It might end up
 * never being scheduled at all.
 *
 * A kernel that has no use for a work item, e.g. because it executes all of
 * the work right away, may return any value other than UACPI_NULL without
 * allocating anything.
 *
 * Both of these might be invoked by the deferred work itself, but are never
 * used in interrupt contexts, unless that's where the kernel chooses to
 * execute said work.
 */
uacpi_handle uacpi_kernel_create_work_item(void) {
    return (uacpi_handle) heap_alloc(sizeof(uacpi_work_item_t));
}

void uacpi_kernel_free_work_item(uacpi_handle handle) {
    heap_free(handle, sizeof(uacpi_work_item_t));
}
