#include <arch/x86_64/hardware/ioapic.h>
#include <arch/x86_64/io.h>
#include <common/arch.h>
#include <common/log.h>
#include <lib/list.h>
#include <lib/types.h>
#include <memory/vm.h>
#include <stdint.h>
#include <uacpi/acpi.h>
#include <uacpi/tables.h>

#include "memory/heap.h"

// I/O APIC registers
#define IOAPIC_REG_ID 0x00
#define IOAPIC_REG_VER 0x01
#define IOAPIC_REG_ARBITRATION 0x02
#define IOAPIC_REG_REDTBL_BASE 0x10

// I/O APIC Redirection Entry flags
#define IOAPIC_DEST_SHIFT 56
#define IOAPIC_DEST_MASK 0xFF00000000000000
#define IOAPIC_MASKED (1 << 16)
#define IOAPIC_TRIGGER_LEVEL (1 << 15)
#define IOAPIC_REMOTE_IRR (1 << 14)
#define IOAPIC_PIN_POLARITY (1 << 13)
#define IOAPIC_DELIVERY_STATUS (1 << 12)
#define IOAPIC_DEST_MODE_LOG (1 << 11)
#define IOAPIC_DELIVERY_MODE (0x700)
#define IOAPIC_VECTOR_MASK 0xFF

// Delivery modes
#define IOAPIC_DELIVERY_FIXED 0x000
#define IOAPIC_DELIVERY_LOWEST 0x100
#define IOAPIC_DELIVERY_SMI 0x200
#define IOAPIC_DELIVERY_NMI 0x400
#define IOAPIC_DELIVERY_INIT 0x500
#define IOAPIC_DELIVERY_EXTINT 0x700

typedef struct {
    uint8_t id;
    uint32_t gsi_base;
    uint32_t gsi_count;
    virt_addr_t mmio_base;

    list_node_t list_node;
} ioapic_t;

#define LEGACY_POLARITY (0b11)
#define LEGACY_TRIGGER (0b11 << 2)
#define LEGACY_POLARITY_HIGH 0b1
#define LEGACY_POLARITY_LOW 0b11
#define LEGACY_TRIGGER_EDGE (0b1 << 2)
#define LEGACY_TRIGGER_LEVEL (0b11 << 2)

typedef struct {
    uint8_t gsi;
    uint16_t flags;
} legacy_irq_translation_t;

static legacy_irq_translation_t g_legacy_irq_map[16] = {
    { 0,  0 },
    { 1,  0 },
    { 2,  0 },
    { 3,  0 },
    { 4,  0 },
    { 5,  0 },
    { 6,  0 },
    { 7,  0 },
    { 8,  0 },
    { 9,  0 },
    { 10, 0 },
    { 11, 0 },
    { 12, 0 },
    { 13, 0 },
    { 14, 0 },
    { 15, 0 }
};


list_t g_ioapics = LIST_INIT;

static void ioapic_write(ioapic_t* ioapic, uint8_t reg, uint32_t data) {
    arch_io_mem_write_u32(ioapic->mmio_base, (uint32_t) reg);
    arch_io_mem_write_u32(ioapic->mmio_base + 0x10, data);
}

static uint32_t ioapic_read(ioapic_t* ioapic, uint8_t reg) {
    arch_io_mem_write_u32(ioapic->mmio_base, (uint32_t) reg);
    return arch_io_mem_read_u32(ioapic->mmio_base + 0x10);
}

static void ioapic_set_entry(ioapic_t* ioapic, uint8_t index, uint64_t data) {
    ioapic_write(ioapic, IOAPIC_REG_REDTBL_BASE + (index * 2), (uint32_t) data);
    ioapic_write(ioapic, IOAPIC_REG_REDTBL_BASE + (index * 2) + 1, (uint32_t) (data >> 32));
}

static ioapic_t* find_ioapic_by_gsi(uint32_t gsi, uint32_t* entry) {
    LIST_FOR_EACH(&g_ioapics, ioapic_entry) {
        ioapic_t* ioapic = CONTAINER_OF(ioapic_entry, ioapic_t, list_node);
        if(gsi >= ioapic->gsi_base && gsi < ioapic->gsi_base + ioapic->gsi_count) {
            *entry = gsi - ioapic->gsi_base;
            return ioapic;
        }
    }

    arch_panic("No ioapic found for gsi %d\n", gsi);
}

static void init_ioapic(struct acpi_madt_ioapic* entry) {
    virt_addr_t mmio_virt = (virt_addr_t) vm_map_direct(g_vm_global_address_space, VM_NO_HINT, PAGE_SIZE_DEFAULT, VM_PROT_RW, VM_CACHE_DISABLE, entry->address, VM_FLAG_MMIO);
    if(mmio_virt == 0) { arch_panic("Failed to map ioapic MMIO region"); }

    ioapic_t* ioapic = (ioapic_t*) heap_alloc(sizeof(ioapic_t));
    ioapic->id = entry->id;
    ioapic->mmio_base = mmio_virt;
    ioapic->gsi_base = entry->gsi_base;

    uint32_t ver = ioapic_read(ioapic, IOAPIC_REG_VER);
    ioapic->gsi_count = ((ver >> 16) & 0xFF) + 1;

    LOG_INFO("ioapic[%u] mmio: 0x%x -> 0x%lx ver: 0x%08x, gsi range: %u-%u\n", ioapic->id, entry->address, mmio_virt, ver & 0xff, ioapic->gsi_base, ioapic->gsi_count);
    for(uint32_t i = 0; i < ioapic->gsi_count; i++) { ioapic_set_entry(ioapic, i, IOAPIC_MASKED); }

    list_push_back(&g_ioapics, &ioapic->list_node);
}

static uacpi_iteration_decision dump_madt_entry(uacpi_handle handle, struct acpi_entry_hdr* hdr) {
    (void) handle;
    if(hdr->type == ACPI_MADT_ENTRY_TYPE_LAPIC) {
        struct acpi_madt_lapic* mhdr = (struct acpi_madt_lapic*) (hdr);
        LOG_INFO("MADT/lapic 0x%p (%d) %d | %d %d 0x%x\n", hdr, hdr->type, hdr->length, mhdr->uid, mhdr->id, mhdr->flags);
    } else if(hdr->type == ACPI_MADT_ENTRY_TYPE_IOAPIC) {
        struct acpi_madt_ioapic* mhdr = (struct acpi_madt_ioapic*) (hdr);
        LOG_INFO("MADT/ioapic 0x%p (%d) %d | %d 0x%x 0x%x\n", hdr, hdr->type, hdr->length, mhdr->id, mhdr->address, mhdr->gsi_base);
    } else if(hdr->type == ACPI_MADT_ENTRY_TYPE_INTERRUPT_SOURCE_OVERRIDE) {
        struct acpi_madt_interrupt_source_override* mhdr = (struct acpi_madt_interrupt_source_override*) (hdr);
        LOG_INFO("MADT/iso 0x%p (%d) %d | %d %d 0x%x 0x%x\n", hdr, hdr->type, hdr->length, mhdr->bus, mhdr->source, mhdr->gsi, mhdr->flags);
    } else if(hdr->type == ACPI_MADT_ENTRY_TYPE_LAPIC_NMI) {
        struct acpi_madt_lapic_nmi* mhdr = (struct acpi_madt_lapic_nmi*) (hdr);
        LOG_INFO("MADT/lapicnmi 0x%p (%d) %d | %d %d 0x%x\n", hdr, hdr->type, hdr->length, mhdr->uid, mhdr->flags, mhdr->lint);
    } else {
        LOG_INFO("MADT/unknown 0x%p (%d) %d\n", hdr, hdr->type, hdr->length);
    }

    return UACPI_ITERATION_DECISION_CONTINUE;
}

static uacpi_iteration_decision first_madt_pass(uacpi_handle handle, struct acpi_entry_hdr* hdr) {
    (void) handle;
    if(hdr->type == ACPI_MADT_ENTRY_TYPE_IOAPIC) {
        struct acpi_madt_ioapic* mhdr = (struct acpi_madt_ioapic*) (hdr);
        init_ioapic(mhdr);
    }

    return UACPI_ITERATION_DECISION_CONTINUE;
}

static uacpi_iteration_decision second_madt_pass(uacpi_handle handle, struct acpi_entry_hdr* hdr) {
    (void) handle;
    if(hdr->type == ACPI_MADT_ENTRY_TYPE_INTERRUPT_SOURCE_OVERRIDE) {
        struct acpi_madt_interrupt_source_override* iso_entry = (struct acpi_madt_interrupt_source_override*) hdr;
        LOG_INFO("ISO: IRQ %d -> GSI %d\n", iso_entry->source, iso_entry->gsi);
        assert(iso_entry->source < 16 && "legacy IRQ source must be < 16");
        g_legacy_irq_map[iso_entry->source].gsi = iso_entry->gsi;
        g_legacy_irq_map[iso_entry->source].flags = iso_entry->flags;
    }
    return UACPI_ITERATION_DECISION_CONTINUE;
}


void arch_ioapic_init() {
    uacpi_table tbl;
    uacpi_table_find_by_signature(ACPI_MADT_SIGNATURE, &tbl);
    if(tbl.hdr == nullptr) { arch_panic("Failed to find MADT\n"); }
    uacpi_for_each_subtable(tbl.hdr, sizeof(struct acpi_madt), dump_madt_entry, nullptr);
    uacpi_for_each_subtable(tbl.hdr, sizeof(struct acpi_madt), first_madt_pass, nullptr);
    uacpi_for_each_subtable(tbl.hdr, sizeof(struct acpi_madt), second_madt_pass, nullptr);

    LOG_OKAY("setup %zu ioapics\n", g_ioapics.count);
}

void arch_ioapic_map_gsi(uint8_t gsi, uint8_t lapic_id, bool polarity, bool trigger_mode, uint8_t vector) {
    uint64_t redirect_entry = vector;
    redirect_entry &= ~((uint64_t) 0xFF << 56);
    redirect_entry |= ((uint64_t) lapic_id) << 56;

    if(polarity) redirect_entry |= IOAPIC_PIN_POLARITY;
    if(!trigger_mode) redirect_entry |= IOAPIC_TRIGGER_LEVEL;
    uint32_t local_index;
    ioapic_t* ioapic = find_ioapic_by_gsi(gsi, &local_index);
    assert(ioapic && "ioapic must exist for gsi");
    ioapic_set_entry(ioapic, local_index, redirect_entry);
}

void arch_ioapic_map_legacy_irq(uint8_t irq, uint8_t lapic_id, bool fallback_polarity, bool fallback_trigger_mode, uint8_t vector) {
    if(irq < 16) {
        switch(g_legacy_irq_map[irq].flags & LEGACY_POLARITY) {
            case LEGACY_POLARITY_LOW:  fallback_polarity = true; break;
            case LEGACY_POLARITY_HIGH: fallback_polarity = false; break;
        }
        switch(g_legacy_irq_map[irq].flags & LEGACY_TRIGGER) {
            case LEGACY_TRIGGER_EDGE:  fallback_trigger_mode = true; break;
            case LEGACY_TRIGGER_LEVEL: fallback_trigger_mode = false; break;
        }
        irq = g_legacy_irq_map[irq].gsi;
    }
    arch_ioapic_map_gsi(irq, lapic_id, fallback_polarity, fallback_trigger_mode, vector);
}
