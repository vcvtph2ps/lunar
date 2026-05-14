#include <arch/cr.h>
#include <arch/msr.h>
#include <common/helpers.h>
#include <log.h>
#include <protocol/bootinfo.h>
#include <stddef.h>
#include <stdint.h>

typedef struct [[gnu::packed, gnu::aligned(16)]] {
    uint64_t asdf;
} page_t;

typedef struct [[gnu::packed, gnu::aligned(16)]] {
    uint64_t asdf;
    uint64_t asd;
    uint64_t asf;
} cpu_local_t;

const size_t g_bootinfo_pagedb_entry_size = sizeof(page_t);
const size_t g_bootinfo_cpulocal_entry_size = sizeof(cpu_local_t);

ATOMIC static uint32_t g_ap_init_lock = 0;

[[noreturn]] void kernel_entry_ap(uint64_t core_id) {
    while(ATOMIC_LOAD(&g_ap_init_lock, ATOMIC_ACQUIRE) == 0);

    log_print("kernel booted on core %d :3\n", core_id);
    log_print("cr0=0x%016lx\n", arch_cr_read_cr0());
    log_print("cr4=0x%016lx\n", arch_cr_read_cr4());
    log_print("xcr0=0x%016lx\n", arch_cr_read_xcr0());
    log_print("efer=0x%016lx\n", arch_msr_read(ARCH_MSR_EFER));
    log_print("active_gs=0x%016lx\n", arch_msr_read(ARCH_MSR_ACTIVE_GS_BASE));
    log_print_raw("\n");

    while(1);
}

[[noreturn]] void kernel_entry(bootinfo_t* boot_info, uint64_t core_id) {
    if(core_id != 0) {
        kernel_entry_ap(core_id);
    }

    log_print("kernel booted on core %d :3\n", core_id);

    log_print("boot_timestamp=%d\n", boot_info->boot_timestamp);
    log_print_raw("\n");
    log_print("rdsp_physical=0x%016lx\n", boot_info->rdsp_physical);
    log_print_raw("\n");
    log_print("hhdm_offset=0x%016lx\n", boot_info->hhdm_offset);
    log_print("hhdm_size=0x%016lx\n", boot_info->hhdm_size);
    log_print_raw("\n");
    log_print("pfndb_start=0x%016lx\n", boot_info->pfndb_start);
    log_print("pfndb_size=0x%016lx\n", boot_info->pfndb_size);
    log_print_raw("\n");
    log_print("kernel_segment_count=%zu\n", boot_info->kernel_segment_count);
    for(size_t i = 0; i < boot_info->kernel_segment_count; i++) {
        bootinfo_segment_t* segment = &boot_info->kernel_segments[i];
        log_print(
            "kernel_segment[%zu]: paddr=0x%016lx, vaddr=0x%016lx, size=0x%016lx, flags=%c%c%c\n",
            i,
            segment->paddr,
            segment->vaddr,
            segment->size,
            (segment->flags & BOOTINFO_SEGMENT_FLAG_READ) ? 'r' : '-',
            (segment->flags & BOOTINFO_SEGMENT_FLAG_WRITE) ? 'w' : '-',
            (segment->flags & BOOTINFO_SEGMENT_FLAG_EXECUTE) ? 'x' : '-'
        );
    }
    log_print_raw("\n");
    log_print("mm_entry_count=%zu\n", boot_info->mm_entry_count);
    for(size_t i = 0; i < boot_info->mm_entry_count; i++) {
        bootinfo_mm_entry_t* entry = &boot_info->mm_entries[i];
        log_print("mm_entry[%zu]: paddr=0x%016lx, size=0x%016lx, type=%d\n", i, entry->phys_base, entry->length, entry->type);
    }

    log_print_raw("\n");
    log_print("framebuffer_count=%zu\n", boot_info->framebuffer_count);
    for(size_t i = 0; i < boot_info->framebuffer_count; i++) {
        bootinfo_framebuffer_t* framebuffer = &boot_info->framebuffers[i];
        log_print("framebuffer[%zu]: vaddr=0x%016lx, paddr=0x%016lx, width=%d, height=%d, pitch=%d, format=", i, framebuffer->vaddr, framebuffer->paddr, framebuffer->width, framebuffer->height, framebuffer->pitch);

        uint32_t bit_position = 0;
        for(int i = 0; i < 3; i++) {
            if(framebuffer->red_position == bit_position) {
                log_print_raw("r%d", framebuffer->red_size);
                bit_position += framebuffer->red_size;
            } else if(framebuffer->green_position == bit_position) {
                log_print_raw("g%d", framebuffer->green_size);
                bit_position += framebuffer->green_size;
            } else if(framebuffer->blue_position == bit_position) {
                log_print_raw("b%d", framebuffer->blue_size);
                bit_position += framebuffer->blue_size;
            }
        }
        log_print_raw("\n");
    }
    log_print_raw("\n");
    log_print("module_count=%zu\n", boot_info->module_count);

    for(size_t i = 0; i < boot_info->module_count; i++) {
        bootinfo_module_t* module = &boot_info->modules[i];
        log_print("module[%zu]: name=%s, phys_addr=0x%016lx, size=0x%016lx\n", i, module->name, module->phys_addr, module->size);
    }

    log_print_raw("\n");
    log_print("cpu_count=%d\n", boot_info->core_count);
    log_print_raw("\n");

    log_print("cr0=0x%016lx\n", arch_cr_read_cr0());
    log_print("cr4=0x%016lx\n", arch_cr_read_cr4());
    log_print("xcr0=0x%016lx\n", arch_cr_read_xcr0());
    log_print("efer=0x%016lx\n", arch_msr_read(ARCH_MSR_EFER));
    log_print("active_gs=0x%016lx\n", arch_msr_read(ARCH_MSR_ACTIVE_GS_BASE));
    log_print_raw("\n");

    ATOMIC_STORE(&g_ap_init_lock, 1, ATOMIC_RELEASE);

    while(1);
}
