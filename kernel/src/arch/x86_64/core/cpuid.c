#include <arch/internal/cpuid.h>
#include <lib/string.h>
#include <stdbool.h>
#include <stdint.h>

[[nodiscard]] uint32_t arch_cpuid(arch_cpuid_leaf_t leaf, uint32_t subleaf, arch_cpuid_reg_t reg) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(leaf), "c"(subleaf));

    switch(reg) {
        case ARCH_CPUID_EAX: return eax;
        case ARCH_CPUID_EBX: return ebx;
        case ARCH_CPUID_ECX: return ecx;
        case ARCH_CPUID_EDX: return edx;
    }
    __builtin_unreachable();
}

[[nodiscard]] uint32_t arch_cpuid_get_feature_value(arch_cpuid_feature_t feature) {
    return arch_cpuid((arch_cpuid_leaf_t) feature.leaf, feature.subleaf, feature.reg) & feature.mask;
}
[[nodiscard]] bool arch_cpuid_is_feature_supported(arch_cpuid_feature_t feature) {
    return (arch_cpuid((arch_cpuid_leaf_t) feature.leaf, feature.subleaf, feature.reg) & feature.mask) != 0;
}

[[nodiscard]] const char* arch_cpuid_get_vendor_string() {
    static char vendor[13] = { 0 };
    *(uint32_t*) &vendor[0] = arch_cpuid(ARCH_CPUID_VENDOR_ID, 0, ARCH_CPUID_EBX);
    *(uint32_t*) &vendor[4] = arch_cpuid(ARCH_CPUID_VENDOR_ID, 0, ARCH_CPUID_EDX);
    *(uint32_t*) &vendor[8] = arch_cpuid(ARCH_CPUID_VENDOR_ID, 0, ARCH_CPUID_ECX);
    return vendor;
}

[[nodiscard]] const char* arch_cpuid_get_name_string() {
    uint32_t max_ext_leaf = arch_cpuid(0x80000000, 0, ARCH_CPUID_EAX);
    if(max_ext_leaf < 0x80000004) { return "Unknown CPU :("; }

    static char name[49];

    char* ptr = name;

    for(uint32_t i = 0x80000002; i <= 0x80000004; i++) {
        uint64_t eax = arch_cpuid((arch_cpuid_leaf_t) i, 0, ARCH_CPUID_EAX);
        uint64_t ebx = arch_cpuid((arch_cpuid_leaf_t) i, 0, ARCH_CPUID_EBX);
        uint64_t ecx = arch_cpuid((arch_cpuid_leaf_t) i, 0, ARCH_CPUID_ECX);
        uint64_t edx = arch_cpuid((arch_cpuid_leaf_t) i, 0, ARCH_CPUID_EDX);
        memcpy(ptr, &eax, 4);
        memcpy(ptr + 4, &ebx, 4);
        memcpy(ptr + 8, &ecx, 4);
        memcpy(ptr + 12, &edx, 4);
        ptr += 16;
    }

    name[48] = '\0';
    return name;
}

[[nodiscard]] arch_cpuid_vendor_t arch_cpuid_get_vendor() {
    const char* vendor_str = arch_cpuid_get_vendor_string();
    if(strcmp(vendor_str, "GenuineIntel") == 0) {
        return ARCH_CPUID_VENDOR_INTEL;
    } else if(strcmp(vendor_str, "AuthenticAMD") == 0) {
        return ARCH_CPUID_VENDOR_AMD;
    } else {
        return ARCH_CPUID_VENDOR_UNKNOWN;
    }
}
