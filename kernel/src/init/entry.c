#include <arch/internal/cr.h>
#include <arch/internal/msr.h>
#include <common/arch.h>
#include <common/cpu_local.h>
#include <common/log.h>
#include <lib/helpers.h>
#include <memory/pagedb.h>
#include <protocol/bootinfo.h>
#include <stddef.h>
#include <stdint.h>

[[gnu::used, gnu::section("prekernel_boot_info")]] static const bootinfo_kernel_info_t g_prekernel_boot_info = {
    .pagedb_entry_size = sizeof(pagedb_page_t),
    .cpu_local_size = sizeof(arch_cpu_local_t),
};

ATOMIC static uint32_t g_ap_init_lock = 0;

[[noreturn]] void kernel_entry_ap(uint64_t core_id) {
    while(ATOMIC_LOAD(&g_ap_init_lock, ATOMIC_ACQUIRE) == 0);
    cpu_local_init(core_id);

    LOG_STRC("kernel booted on core %ld :3\n", core_id);
    LOG_STRC("cr0=0x%016lx\n", arch_cr_read_cr0());
    LOG_STRC("cr4=0x%016lx\n", arch_cr_read_cr4());
    LOG_STRC("xcr0=0x%016lx\n", arch_cr_read_xcr0());
    LOG_STRC("efer=0x%016lx\n", arch_msr_read(ARCH_MSR_EFER));
    LOG_STRC("active_gs=0x%016lx\n", arch_msr_read(ARCH_MSR_ACTIVE_GS_BASE));

    arch_init_ap(core_id);
}

bootinfo_t* g_init_boot_info;

[[noreturn]] void kernel_entry(bootinfo_t* boot_info, uint64_t core_id) {
    if(core_id != 0) { kernel_entry_ap(core_id); }

    g_init_boot_info = boot_info;
    cpu_local_init_bsp(boot_info->cpulocal_start);
    log_init();

    LOG_STRC("kernel booted on core %u :3\n", CPU_LOCAL_READ(core_id));

    LOG_STRC("boot_timestamp=%ld\n", boot_info->boot_timestamp);
    LOG_STRC("rdsp_physical=0x%016lx\n", boot_info->rdsp_physical);
    LOG_STRC("hhdm_offset=0x%016lx\n", boot_info->hhdm_offset);
    LOG_STRC("hhdm_size=0x%016lx\n", boot_info->hhdm_size);
    LOG_STRC("pfndb_start=0x%016lx\n", boot_info->pfndb_start);
    LOG_STRC("pfndb_size=0x%016lx\n", boot_info->pfndb_size);
    LOG_STRC("kernel_segment_count=%zu\n", boot_info->kernel_segment_count);
    for(size_t i = 0; i < boot_info->kernel_segment_count; i++) {
        bootinfo_segment_t* segment = &boot_info->kernel_segments[i];
        LOG_STRC(
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
    LOG_STRC("mm_entry_count=%zu\n", boot_info->mm_entry_count);
    for(size_t i = 0; i < boot_info->mm_entry_count; i++) {
        bootinfo_mm_entry_t* entry = &boot_info->mm_entries[i];
        LOG_STRC("mm_entry[%zu]: paddr=0x%016lx, size=0x%016lx, type=%ld\n", i, entry->phys_base, entry->length, entry->type);
    }

    LOG_STRC("framebuffer_count=%zu\n", boot_info->framebuffer_count);
    for(size_t i = 0; i < boot_info->framebuffer_count; i++) {
        bootinfo_framebuffer_t* framebuffer = &boot_info->framebuffers[i];
        LOG_STRC("framebuffer[%zu]: vaddr=0x%016lx, paddr=0x%016lx, width=%d, height=%d, pitch=%d, format=", i, (uintptr_t) framebuffer->vaddr, framebuffer->paddr, framebuffer->width, framebuffer->height, framebuffer->pitch);
    }
    LOG_STRC("module_count=%zu\n", boot_info->module_count);

    for(size_t i = 0; i < boot_info->module_count; i++) {
        bootinfo_module_t* module = &boot_info->modules[i];
        LOG_STRC("module[%zu]: name=%s, phys_addr=0x%016lx, size=0x%016lx\n", i, module->name, module->phys_addr, module->size);
    }

    LOG_STRC("cpu_count=%ld\n", boot_info->core_count);

    LOG_STRC("cr0=0x%016lx\n", arch_cr_read_cr0());
    LOG_STRC("cr4=0x%016lx\n", arch_cr_read_cr4());
    LOG_STRC("xcr0=0x%016lx\n", arch_cr_read_xcr0());
    LOG_STRC("efer=0x%016lx\n", arch_msr_read(ARCH_MSR_EFER));
    LOG_STRC("active_gs=0x%016lx\n", arch_msr_read(ARCH_MSR_ACTIVE_GS_BASE));

    pagedb_init();

    ATOMIC_STORE(&g_ap_init_lock, 1, ATOMIC_RELEASE);

    arch_init_bsp();
}
