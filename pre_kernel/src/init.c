#include <ap.h>
#include <arch/gdt.h>
#include <arch/machine.h>
#include <arch/msr.h>
#include <common/mem.h>
#include <lib/helpers.h>
#include <lib/math.h>
#include <log.h>
#include <memory/pmm.h>
#include <memory/ptm.h>
#include <protocol/bootinfo.h>

void pk_tartarus_map_kernel();

#define CORE_STACK_PGCNT 16

[[noreturn]] void x86_64_kernel_handoff(void (*entry)(bootinfo_t*, uint64_t core_id), void* stack, uintptr_t page_tables, bootinfo_t* boot_info, uint64_t core_id);

ATOMIC static uint32_t g_ap_init_lock = 0;

bootinfo_t* g_pk_boot_info = nullptr;

[[noreturn]] void pk_init_ap(pk_ap_boot_info_t* boot_info) {
    while(ATOMIC_LOAD(&g_ap_init_lock, ATOMIC_ACQUIRE) == 0);

    pk_machine_init((uintptr_t) boot_info->cpu_local);

    x86_64_kernel_handoff(kernel_entry, (void*) boot_info->ap_stack, g_ptm.tplt, nullptr, boot_info->core_id);
}

bool pk_tartarus_core_is_bsp(uint64_t tartarus_core_index);
void pk_tartarus_start_ap(uint64_t tartarus_core_idnex, pk_ap_boot_info_t* boot_info);

[[noreturn]] void pk_init(bootinfo_t* boot_info) {
    g_pk_boot_info = boot_info;
    pk_log_print("Hai :333\n");

    for(size_t i = 0; i < g_pmm_map_size; i++) {
        pmm_map_entry_t* entry = &g_pmm_map[i];
        pk_log_print("pmm_entry[%zu]: base=0x%016lx, length=0x%016lx, type=%u\n", i, entry->base, entry->length, entry->type);
    }

    pk_ptm_init();

    for(size_t i = 0; i < boot_info->kernel_segment_count; i++) {
        bootinfo_segment_t segment = boot_info->kernel_segments[i];
        uint64_t flags = 0;
        if(segment.flags & BOOTINFO_SEGMENT_FLAG_READ) {
            flags |= PTM_FLAG_READ;
        }
        if(segment.flags & BOOTINFO_SEGMENT_FLAG_WRITE) {
            flags |= PTM_FLAG_WRITE;
        }
        if(segment.flags & BOOTINFO_SEGMENT_FLAG_EXECUTE) {
            flags |= PTM_FLAG_EXEC;
        }

        pk_log_print("kernel segment: 0x%llx -> 0x%llx - %zu [%c%c%c]\n", segment.paddr, segment.vaddr, segment.size, (flags & PTM_FLAG_READ) ? 'r' : '-', (flags & PTM_FLAG_WRITE) ? 'w' : '-', (flags & PTM_FLAG_EXEC) ? 'x' : '-');
        for(uintptr_t i = 0; i < segment.size; i += PTM_PAGE_GRANULARITY) {
            pk_ptm_map(segment.vaddr + i, segment.paddr + i, PTM_PAGE_GRANULARITY, flags);
        }
    }

    void* stack = (pk_pmm_alloc(CORE_STACK_PGCNT) + boot_info->hhdm_offset) + (CORE_STACK_PGCNT * PTM_PAGE_GRANULARITY);

    size_t system_page_count = MATH_ALIGN_UP(boot_info->hhdm_size, PTM_PAGE_GRANULARITY) / PTM_PAGE_GRANULARITY;
    size_t pagedb_size = MATH_ALIGN_UP(system_page_count * g_bootinfo_pagedb_entry_size, PTM_PAGE_GRANULARITY);
    void* pagedb = pk_pmm_alloc_ext(pagedb_size / PTM_PAGE_GRANULARITY, PTM_PAGE_GRANULARITY, PMM_MAP_TYPE_USED) + boot_info->hhdm_offset;
    pk_log_print("pagedb entry size: %zu\n", g_bootinfo_pagedb_entry_size);

    boot_info->pfndb_start = (uintptr_t) pagedb;
    boot_info->pfndb_size = pagedb_size;

    size_t cpu_local_block_size = MATH_ALIGN_UP(system_page_count * g_bootinfo_cpulocal_entry_size, PTM_PAGE_GRANULARITY);
    void* cpu_local_block = pk_pmm_alloc_ext(cpu_local_block_size / PTM_PAGE_GRANULARITY, PTM_PAGE_GRANULARITY, PMM_MAP_TYPE_USED) + boot_info->hhdm_offset;
    pk_log_print("cpu_local size: %zu\n", g_bootinfo_cpulocal_entry_size);

    void* ap_boot_info_block = pk_pmm_alloc(MATH_ALIGN_UP(sizeof(pk_ap_boot_info_t) * boot_info->core_count, PTM_PAGE_GRANULARITY) / PTM_PAGE_GRANULARITY) + boot_info->hhdm_offset;

    uint64_t core_id = 1;
    for(size_t i = 0; i < boot_info->core_count; i++) {
        if(pk_tartarus_core_is_bsp(i)) {
            continue;
        }

        pk_log_print("starting ap %d\n", i);

        pk_ap_boot_info_t* ap_boot_info = &((pk_ap_boot_info_t*) ap_boot_info_block)[i];
        ap_boot_info->cpu_local = ((uintptr_t) cpu_local_block) + (core_id * g_bootinfo_cpulocal_entry_size);
        ap_boot_info->ap_stack = (uintptr_t) (pk_pmm_alloc(CORE_STACK_PGCNT) + boot_info->hhdm_offset) + (CORE_STACK_PGCNT * PTM_PAGE_GRANULARITY);
        ap_boot_info->core_id = core_id++;
        pk_tartarus_start_ap(i, ap_boot_info);
    }

    pk_ptm_create_hhdm_mappings();

    size_t pmm_map_entries = g_pmm_map_size;
    void* memory_map_block;
    while(true) {
        size_t memory_map_block_size = MATH_ALIGN_UP(pmm_map_entries, PTM_PAGE_GRANULARITY);
        memory_map_block = pk_pmm_alloc(memory_map_block_size / PTM_PAGE_GRANULARITY) + boot_info->hhdm_offset;
        if(g_pmm_map_size > pmm_map_entries) {
            pk_pmm_free(memory_map_block, memory_map_block_size / PTM_PAGE_GRANULARITY);
            pmm_map_entries *= 2;
            pk_log_print("memory map block too small, trying again with %zu entries\n", pmm_map_entries);
        } else {
            break;
        }
    }

    boot_info->mm_entry_count = g_pmm_map_size;
    boot_info->mm_entries = (bootinfo_mm_entry_t*) memory_map_block;
    for(size_t i = 0; i < g_pmm_map_size; i++) {
        pmm_map_entry_t* entry = &g_pmm_map[i];
        bootinfo_mm_type_t type;
        switch(entry->type) {
            case PMM_MAP_TYPE_BOOTLOADER_RECLAIMABLE: [[fallthrough]];
            case PMM_MAP_TYPE_EFI_RECLAIMABLE:        [[fallthrough]];
            case PMM_MAP_TYPE_FREE:                   type = BOOTINFO_MM_TYPE_USABLE; break;
            case PMM_MAP_TYPE_ALLOCATED:              type = BOOTINFO_MM_TYPE_RECLAIMABLE; break;
            case PMM_MAP_TYPE_USED:                   type = BOOTINFO_MM_TYPE_USED; break;
            case PMM_MAP_TYPE_ACPI_RECLAIMABLE:       type = BOOTINFO_MM_TYPE_ACPI_RECLAIMABLE; break;
            case PMM_MAP_TYPE_ACPI_NVS:               type = BOOTINFO_MM_TYPE_ACPI_NVS; break;
            case PMM_MAP_TYPE_RESERVED:               type = BOOTINFO_MM_TYPE_RESERVED; break;
            case PMM_MAP_TYPE_BAD:                    type = BOOTINFO_MM_TYPE_BAD; break;
        }

        boot_info->mm_entries[i].phys_base = entry->base;
        boot_info->mm_entries[i].length = entry->length;
        boot_info->mm_entries[i].type = type;
    }

    pk_log_print("cpu_local size: %zu\n", g_bootinfo_cpulocal_entry_size);

    pk_machine_init((uintptr_t) cpu_local_block);

    ATOMIC_STORE(&g_ap_init_lock, 1, ATOMIC_RELEASE);
    x86_64_kernel_handoff(kernel_entry, stack, g_ptm.tplt, boot_info, 0);
    while(1);
}
