#include <arch/cpuid.h>
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
