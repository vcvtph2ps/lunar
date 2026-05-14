#include <arch/cpuid.h>
#include <arch/cr.h>
#include <arch/gdt.h>
#include <arch/msr.h>
#include <log.h>
#include <panic.h>
#include <stdint.h>

static void machine_setup_control_registers() {
    if(!arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_FXSR)) {
        pk_panic("FXSR not supported on this CPU!");
    }

    // PE, MP, ET, WP, PG
    arch_cr_write_cr0(0x80010013);

    uint64_t cr4 = arch_cr_read_cr4();
    bool la57 = cr4 & (1 << 12);

    // PAE, OSFXSR, OSXMMEXCPT
    cr4 = 0x620;
    if(la57) {
        cr4 |= 1 << 12; // CR4.LA57
    }

    bool write_xcr0 = false;
    uint64_t xcr0 = 0;

    pk_log_print("Enabled optional features: [");
    if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_UMIP)) {
        cr4 |= (1 << 11); // CR4.UMIP
        pk_log_print_raw("UMIP, ");
    }

    if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_SMEP)) {
        cr4 |= (1 << 20); // CR4.SMEP
        pk_log_print_raw("SMEP, ");
    }

    if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_SMAP)) {
        cr4 |= (1 << 21); // CR4.SMAP
        pk_log_print_raw("SMAP, ");
    }

    if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_XSAVE)) {
        pk_log_print_raw("XSAVE, ");
        cr4 |= 1 << 18; // CR4.OSXSAVE

        xcr0 |= 1 << 0; // XCR0.X87
        xcr0 |= 1 << 1; // XCR0.SSE
        if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_AVX)) {
            pk_log_print_raw("AVX, ");
            xcr0 |= 1 << 2; // XCR0.AVX
        }
        if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_AVX512)) {
            pk_log_print_raw("AVX512, ");
            xcr0 |= 1 << 5; // XCR0.opmask
            xcr0 |= 1 << 6; // XCR0.ZMM_Hi256
            xcr0 |= 1 << 7; // XCR0.Hi16_ZMM
        }
    }
    pk_log_print_raw("\b\b]\n");

    arch_cr_write_cr4(cr4);
    if(write_xcr0) {
        arch_cr_write_xcr0(xcr0);
    }

    arch_msr_write(ARCH_MSR_EFER, arch_msr_read(ARCH_MSR_EFER) | (1 << 11)); // EFER.NXE
}

void arch_ldt_load_ldt(uint16_t ldtr);

void machine_setup_gdt() {
    arch_gdt_ptr_t gdtr;
    gdtr.base = (uintptr_t) &g_arch_gdt_static_data;
    gdtr.limit = sizeof(g_arch_gdt_static_data) - 1;

    arch_gdt_load_gdt(&gdtr, __builtin_offsetof(arch_gdt_t, kernel_code), __builtin_offsetof(arch_gdt_t, kernel_data), 0x00);
    arch_ldt_load_ldt(0x00);
}

void pk_machine_init(uintptr_t cpu_local_ptr) {
    machine_setup_control_registers();
    machine_setup_gdt();
    arch_msr_write(ARCH_MSR_ACTIVE_GS_BASE, cpu_local_ptr);
}
