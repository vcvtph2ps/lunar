#include <arch/riscv64/cpu_local.h>
#include <arch/riscv64/hardware/sbi.h>
#include <arch/riscv64/internal/ap_info.h>
#include <arch/riscv64/internal/csr.h>
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

extern char __global_pointer[] __asm__("__global_pointer$"); // NOLINT

void arch_start_aps() {
    uint64_t core_count = g_init_boot_info->core_count;
    if(core_count <= 1) {
        return;
    }

    for(uint64_t i = 0; i < core_count; i++) {
        cpu_local_get_other((uint32_t) i)->hart_id = (uint32_t) g_init_boot_info->cpus[i].arch_id;
    }

    phys_addr_t trampoline_phys = pmm_alloc_page(PMM_FLAG_ZERO | PMM_FLAG_PANIC);

    size_t apinit_size = (uintptr_t) g_apinit_end - (uintptr_t) g_apinit_start;
    if(apinit_size % _Alignof(arch_ap_info_t) != 0) {
        arch_panic("AP trampoline is not aligned for ap_info_t\n");
    }

    if(apinit_size + sizeof(arch_ap_info_t) > PAGE_SIZE_DEFAULT) {
        arch_panic("AP trampoline does not fit in a single page\n");
    }

    virt_addr_t trampoline_virt = (virt_addr_t) PTM_TO_HHDM(trampoline_phys);
    memory_copy((void*) trampoline_virt, (void*) g_apinit_start, apinit_size);

    // the trampoline starts executing with no paging off, so the low physical page it runs from has to be identity mapped
    ptm_map(g_vm_global_address_space, trampoline_phys, trampoline_phys, PAGE_SIZE_DEFAULT, VM_PROT_RWX, VM_CACHE_NORMAL, VM_PRIVILEGE_KERNEL, true, true);

    arch_ap_info_t* ap_info = (arch_ap_info_t*) (trampoline_virt + apinit_size);
    ap_info->entry = (uintptr_t) init_kernel_entry_ap;
    ap_info->satp = ARCH_CSR_READ(satp);
    ap_info->sstatus = ARCH_CSR_READ(sstatus);
    ap_info->stvec = ARCH_CSR_READ(stvec);
    ap_info->sie = ARCH_CSR_READ(sie);
    ap_info->global_pointer = (uintptr_t) __global_pointer;

    LOG_INFO("starting %lu application processor(s)\n", core_count - 1);
    LOG_INFO("bsp hart id=%u, trampoline=0x%lx, satp=0x%lx, sstatus=0x%lx, stvec=0x%lx, sie=0x%lx\n", CPU_LOCAL_READ(hart_id), (uintptr_t) trampoline_phys, (uintptr_t) ap_info->satp, ap_info->sstatus, ap_info->stvec, ap_info->sie);

    for(uint64_t i = 1; i < core_count; i++) {
        uint32_t hartid = (uint32_t) g_init_boot_info->cpus[i].arch_id;

        void* stack = vm_map_anon(g_vm_global_address_space, VM_NO_HINT, AP_STACK_SIZE, VM_PROT_RW, VM_CACHE_NORMAL, VM_FLAG_NONE);
        if(stack == nullptr) {
            arch_panic("failed to allocate the stack for core %lu\n", i);
        }

        ap_info->stack = (uintptr_t) stack + AP_STACK_SIZE;
        ap_info->core_id = i;
        ATOMIC_STORE(&ap_info->booted_flag, 0, ATOMIC_RELEASE);
        asm volatile("" ::: "memory");

        arch_sbi_ret_t ret = sbi_start_hart(hartid, (uint64_t) trampoline_phys, (uint64_t) trampoline_phys + apinit_size);
        if(ret.error) {
            LOG_FAIL("ap[%lu]: sbi returned \"%s\" (%ld)\n", i, arch_sbi_return_to_string(ret), ret.value);
            arch_panic("failed to start core %lu (hart id %u)\n", i, hartid);
        }

        LOG_OKAY("ap[%lu]: booted (hart id=%u)\n", i, hartid);
    }
    LOG_OKAY("successfully started all cores\n");
}
