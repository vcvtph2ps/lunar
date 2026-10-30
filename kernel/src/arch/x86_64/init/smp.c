#include <arch/x86_64/cpu_local.h>
#include <arch/x86_64/hardware/lapic.h>
#include <arch/x86_64/hardware/time/tsc.h>
#include <arch/x86_64/internal/cpuid.h>
#include <arch/x86_64/internal/cr.h>
#include <arch/x86_64/internal/gdt.h>
#include <common/arch.h>
#include <common/cpu_local.h>
#include <common/init.h>
#include <common/log.h>
#include <lib/helpers.h>
#include <lib/string.h>
#include <lib/types.h>
#include <memory/pmm.h>
#include <memory/ptm.h>
#include <memory/vm.h>
#include <protocol/bootinfo.h>
#include <stddef.h>
#include <stdint.h>

#define AP_STACK_SIZE (16 * PAGE_SIZE_DEFAULT)

#define AP_INIT_DELAY_US 10000
#define AP_BOOT_RETRIES 1000

extern uint8_t g_apinit_start[]; // NOLINT
extern uint8_t g_apinit_end[]; // NOLINT

// @note: has to match `boot_info` in apinit.asm
typedef struct [[gnu::packed]] {
    uintptr_t cr3;
    uintptr_t stack;
    uintptr_t entry;
    uintptr_t core_boot_info;

    uint8_t booted_flag;
    uint8_t set_nx;
    uint8_t set_la57;

    struct [[gnu::packed]] {
        uint16_t limit;
        uint32_t base;
    } gdtr;

    arch_gdt_t gdt;
} ap_info_t;

// Temporary gdt the trampoline loads while it's in real mode. the layout has to match the selectors used in apinit.asm
#define DEFINE_SEGMENT(ACCESS, FLAGS) { .limit_low = 0, .base_low = 0, .base_mid = 0, .access = (ACCESS), .limit_high_flags = ((FLAGS) << 4), .base_high = 0 }
#define DEFINE_SEGMENT32(ACCESS, FLAGS) { .limit_low = 0xffff, .base_low = 0, .base_mid = 0, .access = (ACCESS), .limit_high_flags = ((FLAGS) << 4) | 0xf, .base_high = 0 }

static const arch_gdt_t g_ap_gdt_template = {
    {}, // null @ 0x00
    DEFINE_SEGMENT32(0x9b, 0xC), // code32 @ 0x08
    DEFINE_SEGMENT32(0x93, 0xC), // data32 @ 0x10
    DEFINE_SEGMENT(0x9b, 0xA), // code64 @ 0x18
    DEFINE_SEGMENT(0x93, 0xC), // data64 @ 0x20
    {}, // user code @ 0x28 (unused)
    {} // tss @ 0x30 (unused)
};

void arch_start_aps() {
    uint64_t core_count = g_init_boot_info->core_count;
    if(core_count <= 1) {
        return;
    }

    for(uint64_t i = 0; i < core_count; i++) {
        cpu_local_get_other((uint32_t) i)->lapic_id = (uint32_t) g_init_boot_info->cpus[i].arch_id;
    }

    size_t apinit_size = (uintptr_t) g_apinit_end - (uintptr_t) g_apinit_start;
    if(apinit_size % _Alignof(ap_info_t) != 0) {
        arch_panic("AP trampoline is not aligned for ap_info_t");
    }
    if(apinit_size + sizeof(ap_info_t) > PAGE_SIZE_DEFAULT) {
        arch_panic("AP trampoline does not fit in a single page");
    }

    phys_addr_t trampoline_phys = pmm_alloc_page_low(PMM_FLAG_PANIC);
    virt_addr_t trampoline_virt = (virt_addr_t) PTM_TO_HHDM(trampoline_phys);
    memory_copy((void*) trampoline_virt, (void*) g_apinit_start, apinit_size);

    // the trampoline starts executing with no paging off, so the low physical page it runs from has to be identity mapped
    ptm_map(g_vm_global_address_space, trampoline_phys, trampoline_phys, PAGE_SIZE_DEFAULT, VM_PROT_RWX, VM_CACHE_NORMAL, VM_PRIVILEGE_KERNEL, true, true);

    ap_info_t* ap_info = (ap_info_t*) (trampoline_virt + apinit_size);
    ap_info->cr3 = arch_cr_read_cr3();
    ap_info->entry = (uintptr_t) init_kernel_entry_ap;
    ap_info->set_nx = arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_NX_PAGES) ? 1 : 0;
    ap_info->set_la57 = (arch_cr_read_cr4() & (1u << 12)) ? 1 : 0;

    memory_copy(&ap_info->gdt, &g_ap_gdt_template, sizeof(arch_gdt_t));
    ap_info->gdtr.limit = sizeof(arch_gdt_t) - 1;
    // The trampoline loads the gdt before paging is enabled, so store its physical address.
    ap_info->gdtr.base = (uint32_t) PTM_FROM_HHDM(&ap_info->gdt);

    LOG_INFO("starting %lu application processor(s)\n", core_count - 1);

    time_timer_t* timer = arch_tsc_timer_get();
    for(uint64_t i = 1; i < core_count; i++) {
        uint32_t lapic_id = (uint32_t) g_init_boot_info->cpus[i].arch_id;

        void* stack = vm_map_anon(g_vm_global_address_space, VM_NO_HINT, AP_STACK_SIZE, VM_PROT_RW, VM_CACHE_NORMAL, VM_FLAG_NONE);
        if(stack == nullptr) {
            arch_panic("failed to allocate the stack for core %lu", i);
        }

        ap_info->stack = (uintptr_t) stack + AP_STACK_SIZE;
        ap_info->core_boot_info = i;
        ATOMIC_STORE(&ap_info->booted_flag, 0, ATOMIC_RELEASE);
        asm volatile("" ::: "memory");

        arch_lapic_send_init(lapic_id);
        timer->sleep(timer, 10000);
        arch_lapic_send_startup(lapic_id, trampoline_phys);

        bool booted = false;
        for(size_t retry = 0; retry < AP_BOOT_RETRIES; retry++) {
            timer->sleep(timer, 10000);
            if(ATOMIC_LOAD(&ap_info->booted_flag, ATOMIC_ACQUIRE) != 0) {
                booted = true;
                break;
            }
        }

        if(!booted) {
            arch_panic("failed to start core %lu (lapic id %u)", i, lapic_id);
        }
        LOG_OKAY("started core %lu (lapic id %u)\n", i, lapic_id);
    }
}
