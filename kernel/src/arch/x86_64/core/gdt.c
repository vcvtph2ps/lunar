#include <arch/cpu_local.h>
#include <arch/internal/gdt.h>
#include <lib/string.h>
#include <lib/types.h>
#include <memory/vm.h>

#define DEFINE_SEGMENT(ACCESS, FLAGS) { .limit_low = 0, .base_low = 0, .base_mid = 0, .access = (ACCESS), .limit_high_flags = ((FLAGS) << 4), .base_high = 0 }

const arch_gdt_t g_arch_gdt_static_data = {
    {}, // null @ 0x00
    DEFINE_SEGMENT(0x9b, 0xA), // lmode kernel code @ 0x08
    DEFINE_SEGMENT(0x93, 0xC), // lmode kernel data @ 0x10
    DEFINE_SEGMENT(0xfb, 0xA), // cmode user code @ 0x18
    DEFINE_SEGMENT(0xf3, 0xC), // lmode user data @ 0x20
    DEFINE_SEGMENT(0xfb, 0xA), // lmode user code @ 0x28
    {} // tss @ 0x30
};

static void gdt_set_tss(arch_gdt_t* gdt, arch_gdt_tss_t* tss) {
    uint32_t tss_limit = sizeof(arch_gdt_tss_t) - 1;

    gdt->tss.entry.limit_low = tss_limit & 0xFFFF;
    gdt->tss.entry.base_low = (uint16_t) ((uintptr_t) tss & 0xFFFF);
    gdt->tss.entry.base_mid = (uint8_t) (((uintptr_t) tss >> 16) & 0xFF);
    gdt->tss.entry.access = 0x89; // present, ring 0, type 9
    gdt->tss.entry.limit_high_flags = (uint8_t) ((tss_limit >> 16) & 0x0F);
    gdt->tss.entry.base_high = (uint8_t) (((uintptr_t) tss >> 24) & 0xFF);
    gdt->tss.base_extended = (uint32_t) ((uint64_t) tss >> 32);
    gdt->tss.reserved0 = 0;
}

void x86_64_load_gdt(arch_gdt_ptr_t* gdtr, uint16_t cs, uint16_t ds, uint16_t tr);

#define IST_PAGE_COUNT 4

void arch_gdt_init() {
    arch_gdt_t* gdt = &CPU_LOCAL_GET_SELF()->gdt;
    arch_gdt_tss_t* tss = &CPU_LOCAL_GET_SELF()->tss;

    memcpy(gdt, &g_arch_gdt_static_data, sizeof(arch_gdt_t));
    memset(tss, 0, sizeof(arch_gdt_tss_t));

    virt_addr_t ist1_stack_virt = (virt_addr_t) vm_map_anon(g_vm_global_address_space, VM_NO_HINT, IST_PAGE_COUNT * PAGE_SIZE_DEFAULT, VM_PROT_RW, VM_CACHE_NORMAL, true);
    virt_addr_t ist2_stack_virt = (virt_addr_t) vm_map_anon(g_vm_global_address_space, VM_NO_HINT, IST_PAGE_COUNT * PAGE_SIZE_DEFAULT, VM_PROT_RW, VM_CACHE_NORMAL, true);
    virt_addr_t ist3_stack_virt = (virt_addr_t) vm_map_anon(g_vm_global_address_space, VM_NO_HINT, IST_PAGE_COUNT * PAGE_SIZE_DEFAULT, VM_PROT_RW, VM_CACHE_NORMAL, true);

    tss->ist[1] = ist1_stack_virt + (IST_PAGE_COUNT * PAGE_SIZE_DEFAULT); // #NMI
    tss->ist[2] = ist2_stack_virt + (IST_PAGE_COUNT * PAGE_SIZE_DEFAULT); // #DF
    tss->ist[3] = ist3_stack_virt + (IST_PAGE_COUNT * PAGE_SIZE_DEFAULT); // #MCE

    gdt_set_tss(gdt, tss);

    arch_gdt_ptr_t gdtr;
    gdtr.limit = sizeof(arch_gdt_t) - 1;
    gdtr.base = (uint64_t) gdt;
    x86_64_load_gdt(&gdtr, 0x08, 0x10, 0x30);
}
