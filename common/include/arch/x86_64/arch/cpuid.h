#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ARCH_CPUID_EAX = 0,
    ARCH_CPUID_EBX = 1,
    ARCH_CPUID_ECX = 2,
    ARCH_CPUID_EDX = 3
} arch_cpuid_reg_t;

typedef struct {
    uint32_t leaf;
    uint32_t subleaf;
    arch_cpuid_reg_t reg;
    uint32_t mask;
} arch_cpuid_feature_t;

typedef enum {
    ARCH_CPUID_VENDOR_ID = 0x0,
    ARCH_CPUID_GET_FEATURES = 0x1,
    ARCH_CPUID_GET_EXTENDED_FEATURES = 0x7,
    ARCH_CPUID_EXTENDED_PROCESSOR_INFO = 0x80000001
} arch_cpuid_leaf_t;

// NOLINTBEGIN
#define CPUID_FEATURE_DEFINE(name, __leaf, __subleaf, __reg, __bit) const static arch_cpuid_feature_t ARCH_CPUID_FEATURE_##name = { .leaf = __leaf, .subleaf = __subleaf, .reg = __reg, .mask = (1 << __bit) };
CPUID_FEATURE_DEFINE(X2APIC, ARCH_CPUID_GET_FEATURES, 0, ARCH_CPUID_ECX, 21);
CPUID_FEATURE_DEFINE(LA57, ARCH_CPUID_GET_EXTENDED_FEATURES, 0, ARCH_CPUID_ECX, 16);
CPUID_FEATURE_DEFINE(PDPE1GB_PAGES, ARCH_CPUID_EXTENDED_PROCESSOR_INFO, 0, ARCH_CPUID_EDX, 26)
CPUID_FEATURE_DEFINE(NX_PAGES, ARCH_CPUID_EXTENDED_PROCESSOR_INFO, 0, ARCH_CPUID_EDX, 20)
CPUID_FEATURE_DEFINE(PAT, ARCH_CPUID_GET_FEATURES, 0, ARCH_CPUID_EDX, 16);
CPUID_FEATURE_DEFINE(FXSR, ARCH_CPUID_GET_FEATURES, 0, ARCH_CPUID_EDX, 24);
CPUID_FEATURE_DEFINE(XSAVE, ARCH_CPUID_GET_FEATURES, 0, ARCH_CPUID_ECX, 26);
CPUID_FEATURE_DEFINE(OSXSAVE, ARCH_CPUID_GET_FEATURES, 0, ARCH_CPUID_ECX, 27);
CPUID_FEATURE_DEFINE(AVX, ARCH_CPUID_GET_FEATURES, 0, ARCH_CPUID_ECX, 28);
CPUID_FEATURE_DEFINE(AVX512, ARCH_CPUID_GET_EXTENDED_FEATURES, 0, ARCH_CPUID_EBX, 16);
CPUID_FEATURE_DEFINE(SMAP, ARCH_CPUID_GET_EXTENDED_FEATURES, 0, ARCH_CPUID_EBX, 20);
CPUID_FEATURE_DEFINE(SMEP, ARCH_CPUID_GET_EXTENDED_FEATURES, 0, ARCH_CPUID_EBX, 7);
CPUID_FEATURE_DEFINE(UMIP, ARCH_CPUID_GET_EXTENDED_FEATURES, 0, ARCH_CPUID_ECX, 2);
CPUID_FEATURE_DEFINE(FRED, ARCH_CPUID_GET_EXTENDED_FEATURES, 1, ARCH_CPUID_EAX, 17);
CPUID_FEATURE_DEFINE(LKGS, ARCH_CPUID_GET_EXTENDED_FEATURES, 1, ARCH_CPUID_EAX, 18);
#undef CPUID_FEATURE_DEFINE
// NOLINTEND

/**
 * @brief Checks if a specific CPU feature is supported by the processor.
 * @param feature The CPU feature to check for support.
 * @return true if the feature is supported, false otherwise.
 */
[[nodiscard]] bool arch_cpuid_is_feature_supported(arch_cpuid_feature_t feature);

/**
 * @brief Gets the value of a specific CPU feature.
 * @param feature The CPU feature to get the value of.
 * @return The value of the CPU feature. Only includes the bits specified by the feature's mask.
 */
[[nodiscard]] uint32_t arch_cpuid_get_feature_value(arch_cpuid_feature_t feature);

/**
 * @brief Executes the CPUID instruction with the specified leaf and subleaf, and returns the value of the specified register.
 * @param leaf The CPUID leaf to query.
 * @param subleaf The CPUID subleaf to query.
 * @param reg The CPUID register to return the value of.
 */
[[nodiscard]] uint32_t arch_cpuid(arch_cpuid_leaf_t leaf, uint32_t subleaf, arch_cpuid_reg_t reg);
