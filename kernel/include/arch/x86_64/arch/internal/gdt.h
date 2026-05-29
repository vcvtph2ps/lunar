#pragma once
#include <stdint.h>

typedef struct [[gnu::packed]] {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t limit_high_flags;
    uint8_t base_high;
} arch_gdt_entry_t;

typedef struct [[gnu::packed]] {
    arch_gdt_entry_t entry;
    uint32_t base_extended;
    uint32_t reserved0;
} arch_gdt_system_entry_t;

typedef struct [[gnu::packed]] {
    arch_gdt_entry_t null;
    arch_gdt_entry_t kernel_code;
    arch_gdt_entry_t kernel_data;
    arch_gdt_entry_t user_code_32;
    arch_gdt_entry_t user_data;
    arch_gdt_entry_t user_code;
    arch_gdt_system_entry_t tss;
} arch_gdt_t;

typedef struct [[gnu::packed]] {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base;
} arch_gdt_tss_t;

typedef struct [[gnu::packed]] {
    uint16_t limit;
    uint64_t base;
} arch_gdt_ptr_t;

extern const arch_gdt_t g_arch_gdt_static_data;

void arch_gdt_load_gdt(arch_gdt_ptr_t* gdtr, uint16_t cs, uint16_t ds, uint16_t tr);
