#include <arch/x86_64/cpu_local.h>
#include <arch/x86_64/hardware/lapic.h>
#include <arch/x86_64/internal/cpuid.h>
#include <arch/x86_64/internal/msr.h>
#include <arch/x86_64/interrupts/interrupt.h>
#include <arch/x86_64/io.h>
#include <common/arch.h>
#include <common/assert.h>
#include <common/init.h>
#include <common/log.h>
#include <lib/types.h>
#include <memory/vm.h>
#include <stdint.h>

// lapic registers
#define LAPIC_ID 0x20
#define LAPIC_VERSION 0x30
#define LAPIC_TPR 0x80
#define LAPIC_APR 0x90
#define LAPIC_PPR 0xA0
#define LAPIC_EOI 0xB0
#define LAPIC_RRD 0xC0
#define LAPIC_LDR 0xD0
#define LAPIC_DFR 0xE0
#define LAPIC_SPURIOUS 0xF0
#define LAPIC_ISR 0x100
#define LAPIC_TMR 0x180
#define LAPIC_IRR 0x200
#define LAPIC_ERROR_STATUS 0x280

#define LAPIC_ICR_LOW 0x300
#define LAPIC_ICR_HIGH 0x310
#define LAPIC_X2APIC_ICR 0x300

#define LAPIC_LVT_TIMER 0x320
#define LAPIC_LVT_THERMAL 0x330
#define LAPIC_LVT_PERF 0x340
#define LAPIC_LVT_LINT0 0x350

#define LAPIC_LVT_LINT1 0x360
#define LAPIC_LVT_ERROR 0x370

// Delivery Mode
#define LAPIC_DELMODE_FIXED 0x000
#define LAPIC_DELMODE_LOWEST 0x100
#define LAPIC_DELMODE_SMI 0x200
#define LAPIC_DELMODE_NMI 0x400
#define LAPIC_DELMODE_INIT 0x500
#define LAPIC_DELMODE_STARTUP 0x600
#define LAPIC_DELMODE_EXTINT 0x700

// Destination Mode
#define LAPIC_DESTMODE_PHYSICAL 0x000
#define LAPIC_DESTMODE_LOGICAL 0x800

// Delivery Status
#define LAPIC_STATUS_IDLE 0x000
#define LAPIC_STATUS_PENDING 0x1000

// Level
#define LAPIC_LEVEL_DEASSERT 0x0000
#define LAPIC_LEVEL_ASSERT 0x4000

// Trigger Mode
#define LAPIC_TRIGGER_EDGE 0x0000
#define LAPIC_TRIGGER_LEVEL 0x8000

// Mask
#define LAPIC_MASK_UNMASKED 0x0000
#define LAPIC_MASK_MASKED 0x10000

// Shorthand
#define LAPIC_SHORTHAND_NONE 0x00000
#define LAPIC_SHORTHAND_SELF 0x40000
#define LAPIC_SHORTHAND_ALL_INCL_SELF 0x80000
#define LAPIC_SHORTHAND_ALL_EXCL_SELF 0xC0000

// ARCH_MSR_APIC_BASE_MSR flags
#define APIC_BASE_ADDR_MASK 0xF'FFFF'FFFF'F000
#define APIC_BASE_BSP (1 << 8)
#define APIC_BASE_ENABLE (1 << 11)
#define APIC_BASE_X2APIC (1 << 10)

static bool g_x2apic_mode = false;
static phys_addr_t g_lapic_phys_base;
static virt_addr_t g_lapic_virt_base;

static bool internal_x2apic_supported(void) {
    return arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_X2APIC);
}

static void apic_enable_mode(uint32_t core_id) {
    uint64_t msr = arch_msr_read(ARCH_MSR_APIC_BASE_MSR);

    if(!(msr & APIC_BASE_ENABLE)) {
        msr |= APIC_BASE_ENABLE;
        arch_msr_write(ARCH_MSR_APIC_BASE_MSR, msr);
        msr = arch_msr_read(ARCH_MSR_APIC_BASE_MSR);
    }

    g_x2apic_mode = internal_x2apic_supported();

    if(g_x2apic_mode) {
        msr |= APIC_BASE_X2APIC;
        arch_msr_write(ARCH_MSR_APIC_BASE_MSR, msr);
        if(INIT_CORE_IS_BSP(core_id)) { LOG_INFO("enabling in x2apic mode\n"); }
        return;
    }

    if(INIT_CORE_IS_BSP(core_id)) {
        LOG_INFO("x2apic mode not supported, using xapic mode\n");
        g_lapic_phys_base = msr & 0xFFFFF000;

        uintptr_t address = g_lapic_phys_base;
        size_t offset = address % PAGE_SIZE_DEFAULT;
        if(offset != 0) { address -= offset; }

        g_lapic_virt_base = (virt_addr_t) vm_map_direct(g_vm_global_address_space, VM_NO_HINT, PAGE_SIZE_DEFAULT, VM_PROT_RW, VM_CACHE_DISABLE, g_lapic_phys_base, VM_FLAG_NONE) + offset;
        LOG_INFO("apic base address: 0x%lx -> 0x%lx\n", g_lapic_phys_base, g_lapic_virt_base);
    } else {
        msr = arch_msr_read(ARCH_MSR_APIC_BASE_MSR);
        msr &= ~APIC_BASE_ADDR_MASK;
        msr |= g_lapic_phys_base;
        arch_msr_write(ARCH_MSR_APIC_BASE_MSR, msr);
    }
}

uint32_t arch_lapic_read(uint32_t reg) {
    if(g_x2apic_mode) {
        return (uint32_t) arch_msr_read(ARCH_MSR_X2APIC_BASE_MSR + (reg >> 4));
    } else {
        return arch_io_mem_read_u32(g_lapic_virt_base + reg);
    }
}

void arch_lapic_write(uint32_t reg, uint32_t value) {
    if(g_x2apic_mode) {
        arch_msr_write(ARCH_MSR_X2APIC_BASE_MSR + (reg >> 4), value);
    } else {
        arch_io_mem_write_u32(g_lapic_virt_base + reg, value);
    }
}

static void lapic_write64(uint32_t reg, uint64_t value) {
    if(g_x2apic_mode) {
        arch_msr_write(ARCH_MSR_X2APIC_BASE_MSR + (reg >> 4), value);
    } else {
        arch_panic("lapic_write64 is not supported in xAPIC mode");
    }
}


uint32_t arch_lapic_get_id() {
    if(g_x2apic_mode) {
        return arch_lapic_read(LAPIC_ID);
    } else {
        return (arch_lapic_read(LAPIC_ID) >> 24) & 0xFF;
    }
}

bool arch_lapic_is_bsp() {
    uint64_t msr = arch_msr_read(ARCH_MSR_APIC_BASE_MSR);
    return (msr & APIC_BASE_BSP) != 0;
}


void arch_lapic_eoi() {
    arch_lapic_write(LAPIC_EOI, 0);
}

static void lapic_configure() {
    arch_lapic_write(LAPIC_TPR, 0);
    if(!g_x2apic_mode) {
        arch_lapic_write(LAPIC_DFR, 0xF0000000);
        arch_lapic_write(LAPIC_LDR, 0x01000000);
    }

    arch_lapic_write(LAPIC_LVT_TIMER, LAPIC_MASK_MASKED);
    arch_lapic_write(LAPIC_LVT_THERMAL, LAPIC_MASK_MASKED);
    arch_lapic_write(LAPIC_LVT_PERF, LAPIC_MASK_MASKED);
    arch_lapic_write(LAPIC_LVT_LINT0, LAPIC_MASK_MASKED);
    arch_lapic_write(LAPIC_LVT_LINT1, LAPIC_MASK_MASKED);
    arch_lapic_write(LAPIC_LVT_ERROR, LAPIC_MASK_MASKED);

    uint32_t spurious = arch_lapic_read(LAPIC_SPURIOUS);
    spurious |= (1 << 8); // bit 8: apic enable bit
    spurious |= 0xFF; // vector: 0xFF
    arch_lapic_write(LAPIC_SPURIOUS, spurious);
}

void arch_lapic_init(uint32_t core_id) {
    apic_enable_mode(core_id);
    CPU_LOCAL_WRITE(lapic_id, arch_lapic_get_id());

    lapic_configure();
    LOG_OKAY("initialized in %s mode for lapic %d (bsp)\n", g_x2apic_mode ? "x2APIC" : "xAPIC", arch_lapic_get_id());
}

static void lapic_send_icr(uint32_t apic_id, uint64_t icr) {
    if(g_x2apic_mode) {
        // for x2apic, the destination field is bits 63-32
        icr |= (((uint64_t) apic_id) << 32);
    } else {
        // for xapic, the destination field is bits 63-56
        icr |= (((uint64_t) apic_id) << 56);
    }

    if(g_x2apic_mode) {
        lapic_write64(LAPIC_X2APIC_ICR, icr);
    } else {
        // ughhhhh
        arch_interrupt_state_t irq_state = arch_interrupt_disable();
        arch_io_mem_write_u32(g_lapic_virt_base + LAPIC_ICR_HIGH, (icr >> 32));
        arch_io_mem_write_u32(g_lapic_virt_base + LAPIC_ICR_LOW, (icr & 0xFFFFFFFF));
        arch_interrupt_restore(irq_state);
        while(arch_lapic_read(LAPIC_ICR_LOW) & (1 << 12)) { __builtin_ia32_pause(); }
    }
}

void arch_lapic_send_ipi(uint32_t apic_id, uint8_t vector) {
    uint64_t icr = 0;

    icr |= LAPIC_SHORTHAND_NONE;
    icr |= LAPIC_TRIGGER_EDGE;
    icr |= LAPIC_LEVEL_DEASSERT;
    icr |= LAPIC_DESTMODE_PHYSICAL;
    icr |= LAPIC_DELMODE_FIXED;
    icr |= vector;

    lapic_send_icr(apic_id, icr);
}

void arch_lapic_broadcast_ipi(uint8_t vector) {
    uint64_t icr = 0;

    icr |= LAPIC_SHORTHAND_ALL_EXCL_SELF;
    icr |= LAPIC_TRIGGER_EDGE;
    icr |= LAPIC_LEVEL_DEASSERT;
    icr |= LAPIC_DESTMODE_PHYSICAL;
    icr |= LAPIC_DELMODE_FIXED;
    icr |= vector;

    lapic_send_icr(0, icr);
}
