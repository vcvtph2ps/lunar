#include <arch/riscv64/cpu_local.h>
#include <arch/riscv64/dt.h>
#include <arch/riscv64/hardware/plic.h>
#include <common/arch.h>
#include <common/hardware/io.h>
#include <common/init.h>
#include <common/interrupts/interrupt.h>
#include <common/libfdt.h>
#include <common/log.h>
#include <lib/types.h>
#include <memory/vm.h>
#include <stdbool.h>
#include <stdint.h>
#include <uacpi/acpi.h>
#include <uacpi/status.h>
#include <uacpi/tables.h>
#include <uacpi/uacpi.h>

/// plic registers
#define PLIC_PRIORITY_OFFSET 0x000000
#define PLIC_ENABLE_OFFSET 0x002000
#define PLIC_ENABLE_STRIDE 0x80
#define PLIC_CONTEXT_OFFSET 0x200000
#define PLIC_CONTEXT_STRIDE 0x1000

// Offset of the priority threshold within a context block
#define PLIC_CONTEXT_THRESHOLD_OFFSET 0x0
// Offset of the claim/complete register within a context block
#define PLIC_CONTEXT_CLAIM_OFFSET 0x4

typedef struct {
    io_region_t mmio;
    uint32_t irq_count;
    /// @brief GSI base of this PLIC (0 on the device tree path)
    uint32_t gsi_base;
    bool initialized;
} arch_plic_t;

static arch_plic_t g_plic;
static interrupt_handler_fn_t g_plic_handlers[ARCH_PLIC_MAX_IRQS];
static void* g_plic_contexts[ARCH_PLIC_MAX_IRQS];

static const char* g_plic_compatibles[] = { "sifive,plic-1.0.0", "riscv,plic0", "sifive,plic0", nullptr };

[[nodiscard]] static uint32_t plic_s_context(uint64_t hart_id) {
    return (uint32_t) (hart_id * 2 + 1);
}

[[nodiscard]] static size_t plic_priority_offset(uint32_t irq) {
    return PLIC_PRIORITY_OFFSET + (size_t) irq * sizeof(uint32_t);
}

[[nodiscard]] static size_t plic_enable_offset(uint32_t context, uint32_t irq) {
    return PLIC_ENABLE_OFFSET + (size_t) context * PLIC_ENABLE_STRIDE + (irq / 32) * sizeof(uint32_t);
}

[[nodiscard]] static size_t plic_context_offset(uint32_t context) {
    return PLIC_CONTEXT_OFFSET + (size_t) context * PLIC_CONTEXT_STRIDE;
}

static void plic_set_enable(uint32_t context, uint32_t irq, bool enabled) {
    size_t offset = plic_enable_offset(context, irq);
    uint32_t bit = 1u << (irq % 32);
    uint32_t value = io_read32(&g_plic.mmio, offset);
    if(enabled) {
        value |= bit;
    } else {
        value &= ~bit;
    }
    io_write32(&g_plic.mmio, offset, value);
}

static void plic_for_each_s_context(void (*callback)(uint32_t context, void* ctx), void* ctx) {
    for(uint64_t i = 0; i < g_init_boot_info->core_count; i++) {
        callback(plic_s_context(g_init_boot_info->cpus[i].arch_id), ctx);
    }
}

static void plic_clear_threshold(uint32_t context, void* ctx) {
    (void) ctx;
    io_write32(&g_plic.mmio, plic_context_offset(context) + PLIC_CONTEXT_THRESHOLD_OFFSET, 0);
}

static void plic_external_interrupt_handler(arch_interrupt_frame_t* frame, void* ctx) {
    (void) ctx;

    uint32_t irq = arch_plic_claim();
    if(irq == 0) {
        return;
    }

    if(irq < ARCH_PLIC_MAX_IRQS && g_plic_handlers[irq] != nullptr) {
        g_plic_handlers[irq](frame, g_plic_contexts[irq]);
    } else {
        LOG_WARN("no handler registered for interrupt %u\n", irq);
    }

    arch_plic_complete(irq);
}

typedef struct {
    uint64_t address;
    uint64_t size;
    uint32_t gsi_base;
    uint32_t sources;
    bool found;
} plic_madt_ctx_t;

static uacpi_iteration_decision plic_madt_subtable(uacpi_handle user, struct acpi_entry_hdr* entry) {
    plic_madt_ctx_t* ctx = (plic_madt_ctx_t*) user;
    if(entry->type != ACPI_MADT_ENTRY_TYPE_PLIC) {
        return UACPI_ITERATION_DECISION_CONTINUE;
    }

    const struct acpi_madt_plic* plic = (const struct acpi_madt_plic*) entry;
    ctx->address = plic->address;
    ctx->size = plic->size;
    ctx->gsi_base = plic->gsi_base;
    ctx->sources = plic->sources_count;
    ctx->found = true;
    return UACPI_ITERATION_DECISION_BREAK;
}

[[nodiscard]] static bool plic_from_acpi(plic_madt_ctx_t* out) {
    if(!g_init_boot_info->rdsp_physical || uacpi_get_current_init_level() < UACPI_INIT_LEVEL_SUBSYSTEM_INITIALIZED) {
        return false;
    }

    uacpi_table madt;
    if(uacpi_table_find_by_signature(ACPI_MADT_SIGNATURE, &madt) != UACPI_STATUS_OK) {
        LOG_WARN("ACPI: MADT not found\n");
        return false;
    }

    *out = (plic_madt_ctx_t) { 0 };
    uacpi_for_each_subtable(madt.hdr, sizeof(struct acpi_madt), plic_madt_subtable, out);

    uacpi_table_unref(&madt);
    return out->found;
}

void arch_plic_init(void) {
    if(g_plic.initialized) {
        return;
    }

    uint64_t address = 0;
    uint64_t size = 0;
    uint32_t irq_count = ARCH_PLIC_MAX_IRQS;
    uint32_t gsi_base = 0;

    const void* fdt = arch_dt_blob();
    if(fdt != nullptr) {
        int node = arch_dt_node_offset_by_compatible_list(fdt, -1, g_plic_compatibles);
        if(node < 0) {
            LOG_WARN("no compatible controller found (%d)\n", node);
            return;
        }

        if(!arch_dt_get_reg(fdt, node, &address, &size)) {
            LOG_WARN("controller has no reg property\n");
            return;
        }

        int len = 0;
        const fdt32_t* ndev = (const fdt32_t*) fdt_getprop(fdt, node, "riscv,ndev", &len);
        if(ndev != nullptr && len >= (int) sizeof(fdt32_t)) {
            irq_count = fdt32_to_cpu(*ndev) + 1;
        }
    } else {
        plic_madt_ctx_t acpi;
        if(!plic_from_acpi(&acpi)) {
            LOG_WARN("no PLIC controller found\n");
            return;
        }

        address = acpi.address;
        size = acpi.size;
        gsi_base = acpi.gsi_base;
        if(acpi.sources != 0) {
            irq_count = acpi.sources + 1;
        }
    }

    if(irq_count > ARCH_PLIC_MAX_IRQS) {
        irq_count = ARCH_PLIC_MAX_IRQS;
    }

    virt_addr_t virt = (virt_addr_t) vm_map_direct(g_vm_global_address_space, VM_NO_HINT, ALIGN_UP(size, PAGE_SIZE_DEFAULT), VM_PROT_RW, VM_CACHE_DISABLE, address, VM_FLAG_NO_PAGEDB);
    if(virt == 0) {
        arch_panic("failed to map MMIO region at 0x%lx\n", (unsigned long) address);
    }

    g_plic.mmio = IO_REGION_MMIO(virt);
    g_plic.irq_count = irq_count;
    g_plic.gsi_base = gsi_base;

    // mask every source until a driver claims it
    for(uint32_t irq = 1; irq < g_plic.irq_count; irq++) {
        io_write32(&g_plic.mmio, plic_priority_offset(irq), 0);
    }

    interrupt_set_hardirq_handler(ARCH_INTERRUPT_CAUSE_SEI, plic_external_interrupt_handler, nullptr);
    plic_for_each_s_context(plic_clear_threshold, nullptr);

    g_plic.initialized = true;
    LOG_OKAY("plic mapped 0x%lx -> 0x%lx, %u interrupt sources\n", (unsigned long) address, (unsigned long) virt, g_plic.irq_count);
}

uint32_t arch_plic_gsi_to_irq(uint32_t gsi) {
    if(!g_plic.initialized || gsi < g_plic.gsi_base) {
        return 0;
    }

    uint32_t irq = gsi - g_plic.gsi_base;
    return irq < g_plic.irq_count ? irq : 0;
}

bool arch_plic_is_initialized(void) {
    return g_plic.initialized;
}

void arch_plic_set_handler(uint32_t irq, interrupt_handler_fn_t handler, void* ctx) {
    if(irq == 0 || irq >= ARCH_PLIC_MAX_IRQS) {
        return;
    }

    g_plic_handlers[irq] = handler;
    g_plic_contexts[irq] = ctx;
}

void arch_plic_set_priority(uint32_t irq, uint32_t priority) {
    if(irq == 0 || irq >= g_plic.irq_count || !g_plic.initialized) {
        return;
    }

    io_write32(&g_plic.mmio, plic_priority_offset(irq), priority);
}

typedef struct {
    uint32_t irq;
    bool enable;
} plic_enable_ctx_t;

static void plic_apply_enable(uint32_t context, void* ctx) {
    plic_enable_ctx_t* enable = (plic_enable_ctx_t*) ctx;
    plic_set_enable(context, enable->irq, enable->enable);
}

void arch_plic_enable_irq(uint32_t irq, uint32_t priority) {
    if(irq == 0 || irq >= g_plic.irq_count || !g_plic.initialized) {
        return;
    }

    arch_plic_set_priority(irq, priority);

    plic_enable_ctx_t ctx = { .irq = irq, .enable = true };
    plic_for_each_s_context(plic_apply_enable, &ctx);
}

void arch_plic_disable_irq(uint32_t irq) {
    if(irq == 0 || irq >= g_plic.irq_count || !g_plic.initialized) {
        return;
    }

    plic_enable_ctx_t ctx = { .irq = irq, .enable = false };
    plic_for_each_s_context(plic_apply_enable, &ctx);

    arch_plic_set_priority(irq, 0);
}

uint32_t arch_plic_claim(void) {
    if(!g_plic.initialized) {
        return 0;
    }

    uint32_t context = plic_s_context(CPU_LOCAL_READ(hart_id));
    return io_read32(&g_plic.mmio, plic_context_offset(context) + PLIC_CONTEXT_CLAIM_OFFSET);
}

void arch_plic_complete(uint32_t irq) {
    if(!g_plic.initialized || irq == 0) {
        return;
    }

    uint32_t context = plic_s_context(CPU_LOCAL_READ(hart_id));
    io_write32(&g_plic.mmio, plic_context_offset(context) + PLIC_CONTEXT_CLAIM_OFFSET, irq);
}
