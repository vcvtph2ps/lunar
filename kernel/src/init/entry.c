#include <common/arch.h>
#include <common/cpu_local.h>
#include <common/log.h>
#include <lib/helpers.h>
#include <memory/pagedb.h>
#include <protocol/bootinfo.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __ARCH_X86_64__
#include <arch/internal/cpuid.h>
#endif

#ifdef __ARCH_RISCV64__
extern char __global_pointer[] __asm__("__global_pointer$"); // NOLINT
#endif
[[gnu::used, gnu::section("prekernel_boot_info")]] static const bootinfo_kernel_info_t g_prekernel_boot_info = {
    .pagedb_entry_size = sizeof(pagedb_page_t),
    .cpu_local_size = sizeof(arch_cpu_local_t),
#ifdef __ARCH_RISCV64__
    .global_pointer = (uintptr_t) __global_pointer,
#endif
};

ATOMIC static uint32_t g_ap_init_lock = 0;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-prototypes"

[[noreturn]] static void kernel_entry_ap(uint64_t core_id) {
    while(ATOMIC_LOAD(&g_ap_init_lock, ATOMIC_ACQUIRE) == 0);
    cpu_local_init(core_id);

    LOG_STRC("kernel booted on core %ld :3\n", core_id);
    arch_init_ap(core_id);
}

bootinfo_t* g_init_boot_info;

[[gnu::used, noreturn]] void kernel_entry(bootinfo_t* boot_info, uint64_t core_id) {
    if(core_id != 0) { kernel_entry_ap(core_id); }

    g_init_boot_info = boot_info;
    cpu_local_init_bsp(boot_info->cpulocal_start);
    log_init();

    LOG_STRC("kernel booted on core %u :3\n", CPU_LOCAL_READ(core_id));

#ifdef __ARCH_RISCV64__
    LOG_INFO("Platform: %s (riscv64)\n", boot_info->riscv_base_isa_string);
#else
    arch_cpuid_vendor_t vendor = arch_cpuid_get_vendor();
    if(vendor == ARCH_CPUID_VENDOR_INTEL) {
        LOG_INFO("Platform: intel64 (EM64T)\n");
    } else if(vendor == ARCH_CPUID_VENDOR_AMD) {
        LOG_INFO("Platform: amd64\n");
    } else {
        LOG_INFO("Platform: generic x86_64\n");
    }
#endif

    LOG_INFO("Has acpi: %s\n", g_init_boot_info->rdsp_physical ? "true" : "false");

#ifdef __ARCH_RISCV64__
    LOG_INFO("Has dtb: %s\n", g_init_boot_info->dtb_physical ? "true" : "false");
    LOG_INFO("%zu extensions: ", g_init_boot_info->riscv_extension_count);
    for(size_t i = 0; i < g_init_boot_info->riscv_extension_count; i++) {
        if(i == 0)
            log_print(LOG_LEVEL_INFO, "%s", (*g_init_boot_info->riscv_extentions)[i]);
        else
            log_print(LOG_LEVEL_INFO, ", %s", (*g_init_boot_info->riscv_extentions)[i]);
    }
    log_print(LOG_LEVEL_INFO, "\n");

#endif

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
        LOG_STRC("mm_entry[%zu]: paddr=0x%016lx, end=0x%016lx (0x%lx), type=%ld\n", i, entry->phys_base, entry->phys_base + entry->length, entry->length, entry->type);
    }

    LOG_STRC("framebuffer_count=%zu\n", boot_info->framebuffer_count);
    for(size_t i = 0; i < boot_info->framebuffer_count; i++) {
        bootinfo_framebuffer_t* framebuffer = &boot_info->framebuffers[i];

        LOG_STRC("framebuffer[%zu]: vaddr=0x%016lx, paddr=0x%016lx, width=%d, height=%d, pitch=%d, format=", i, (uintptr_t) framebuffer->vaddr, framebuffer->paddr, framebuffer->width, framebuffer->height, framebuffer->pitch);

        struct fbpixel {
            uint8_t pos;
            uint8_t size;
            char color;
        };

        struct fbpixel pixels[3] = {
            { .pos = framebuffer->red_position,   .size = framebuffer->red_size,   .color = 'r' },
            { .pos = framebuffer->green_position, .size = framebuffer->green_size, .color = 'g' },
            { .pos = framebuffer->blue_position,  .size = framebuffer->blue_size,  .color = 'b' },
        };

        for(size_t i = 0; i < 3; i++) {
            for(size_t j = i + 1; j < 3; j++) {
                if(pixels[j].pos < pixels[i].pos) {
                    struct fbpixel tmp = pixels[i];
                    pixels[i] = pixels[j];
                    pixels[j] = tmp;
                }
            }
        }

        for(size_t i = 0; i < 3; i++) { log_print(LOG_LEVEL_STRC, "%c%u", pixels[i].color, pixels[i].size); }
        log_print(LOG_LEVEL_STRC, "\n");
    }
    LOG_STRC("module_count=%zu\n", boot_info->module_count);

    for(size_t i = 0; i < boot_info->module_count; i++) {
        bootinfo_module_t* module = &boot_info->modules[i];
        LOG_STRC("module[%zu]: name=%s, phys_addr=0x%016lx, size=0x%016lx\n", i, module->name, module->phys_addr, module->size);
    }

    LOG_STRC("cpu_count=%ld\n", boot_info->core_count);

    pagedb_init();

    ATOMIC_STORE(&g_ap_init_lock, 1, ATOMIC_RELEASE);
    arch_init_bsp();
}

#pragma clang diagnostic pop
