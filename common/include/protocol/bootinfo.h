#pragma once
#include <stddef.h>
#include <stdint.h>

#define BOOTINFO_SEGMENT_FLAG_READ (1 << 0)
#define BOOTINFO_SEGMENT_FLAG_WRITE (1 << 1)
#define BOOTINFO_SEGMENT_FLAG_EXECUTE (1 << 2)

typedef struct [[gnu::packed]] {
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t size;
    uint8_t flags;
} bootinfo_segment_t;

typedef struct [[gnu::packed]] {
    void* vaddr;
    uintptr_t paddr;
    size_t size;

    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;

    uint8_t red_position;
    uint8_t red_size;
    uint8_t green_position;
    uint8_t green_size;
    uint8_t blue_position;
    uint8_t blue_size;
} bootinfo_framebuffer_t;

typedef struct [[gnu::packed]] {
    uint64_t paddr;
    uint64_t vaddr;
    uint64_t size;
    uint8_t flags;
} bootinfo_core_t;

typedef struct [[gnu::packed]] {
    char* name;
    uintptr_t phys_addr;
    size_t size;
} bootinfo_module_t;

/// Physical memory map entry type
typedef enum : uint64_t {
    /// Usable and free memory
    BOOTINFO_MM_TYPE_USABLE,

    /// Usable but currently used memory by the kernel or prekernel
    BOOTINFO_MM_TYPE_USED,

    /// Usable memory used by the prekernel that the kernel can take back once it's done with it
    BOOTINFO_MM_TYPE_RECLAIMABLE,

    /// Usable memory used by ACPI
    BOOTINFO_MM_TYPE_ACPI_RECLAIMABLE,

    /// ACPI NVS memory
    BOOTINFO_MM_TYPE_ACPI_NVS,

    /// Reserved by firmware
    BOOTINFO_MM_TYPE_RESERVED,

    /// Memory marked as bad by firmware
    BOOTINFO_MM_TYPE_BAD
} bootinfo_mm_type_t;

typedef struct [[gnu::packed]] {
    bootinfo_mm_type_t type;
    uintptr_t phys_base;
    size_t length;
} bootinfo_mm_entry_t;

typedef struct [[gnu::packed]] {
    uint64_t boot_timestamp;
    uint64_t core_count;

    uintptr_t rdsp_physical;

    uintptr_t hhdm_offset;
    uintptr_t hhdm_size;

    uintptr_t pfndb_start;
    uintptr_t pfndb_size;

    size_t mm_entry_count;
    bootinfo_mm_entry_t* mm_entries;

    size_t kernel_segment_count;
    bootinfo_segment_t* kernel_segments;

    size_t framebuffer_count;
    bootinfo_framebuffer_t* framebuffers;

    size_t module_count;
    bootinfo_module_t* modules;
} bootinfo_t;

extern const size_t g_bootinfo_pagedb_entry_size;
extern const size_t g_bootinfo_cpulocal_entry_size;

[[noreturn]] extern void kernel_entry(bootinfo_t* boot_info, uint64_t core_id); // NOLINT
