#include <arch/x86_64/hardware/fpu.h>
#include <arch/x86_64/internal/cpuid.h>
#include <arch/x86_64/internal/cr.h>
#include <common/assert.h>
#include <common/init.h>
#include <common/log.h>
#include <memory/heap.h>
#include <memory/vm.h>
#include <stddef.h>

size_t g_fpu_area_size;
void (*g_fpu_save)(void* ptr);
void (*g_fpu_load)(void* ptr);

void arch_fpu_save(void* ptr) {
    g_fpu_save(ptr);
}

void arch_fpu_load(void* ptr) {
    g_fpu_load(ptr);
}

[[clang::no_stack_protector]] static inline void xsave(void* ptr) {
    uint32_t eax, edx;
    asm volatile("xgetbv\nxsave (%2)" : "=&a"(eax), "=&d"(edx) : "r"(ptr), "c"(0) : "memory");
}

[[clang::no_stack_protector]] static inline void xrstor(void* ptr) {
    uint32_t eax, edx;
    asm volatile("xgetbv\nxrstor (%2)" : "=&a"(eax), "=&d"(edx) : "r"(ptr), "c"(0) : "memory");
}

[[clang::no_stack_protector]] static inline void fxsave(void* ptr) {
    asm volatile("fxsave (%0)" : : "r"(ptr) : "memory");
}

[[clang::no_stack_protector]] static inline void fxrstor(void* ptr) {
    asm volatile("fxrstor (%0)" : : "r"(ptr) : "memory");
}

static inline void dump_fpu_info() {
    if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_SSE4_2)) {
        LOG_INFO("fpu: sse version \"SSE4.2\"\n");
    } else if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_SSE4_1)) {
        LOG_INFO("fpu: sse version \"SSE4.1\"\n");
    } else if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_SSSE3)) {
        LOG_INFO("fpu: sse version \"SSSE3\"\n");
    } else if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_SSE3)) {
        LOG_INFO("fpu: sse version \"SSE3\"\n");
    } else if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_SSE2)) {
        LOG_INFO("fpu: sse version \"SSE2\"\n");
    } else if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_SSE)) {
        LOG_INFO("fpu: sse version \"SSE\"\n");
    } else {
        LOG_INFO("fpu: sse version \"Unknown\"\n");
    }

    if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_AVX512F)) {
        uint32_t avx512_extentions = arch_cpuid(ARCH_CPUID_GET_EXTENDED_FEATURES, 0, ARCH_CPUID_EBX);
        LOG_INFO(
            "fpu: avx version \"AVX512 with%s%s%s%s%s%s%s\"\n",
            (avx512_extentions & (1u << 17)) ? " DQ" : "",
            (avx512_extentions & (1u << 21)) ? " IFMA" : "",
            (avx512_extentions & (1u << 26)) ? " PF" : "",
            (avx512_extentions & (1u << 27)) ? " ER" : "",
            (avx512_extentions & (1u << 28)) ? " CD" : "",
            (avx512_extentions & (1u << 30)) ? " BW" : "",
            (avx512_extentions & (1u << 31)) ? " VL" : ""
        );
    } else if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_AVX2)) {
        LOG_INFO("fpu: avx version \"AVX2\"\n");
    } else if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_AVX)) {
        LOG_INFO("fpu: avx version \"AVX\"\n");
    } else {
        LOG_INFO("fpu: avx version \"Unknown\"\n");
    }
}

static void set_fpu_machine_state() {
    // enable cr4.OSFXSR & cr4.OSXMMEXCPT
    uint64_t cr4 = arch_cr_read_cr4();
    cr4 |= (1 << 10) | (1 << 9);

    uint64_t xcr0 = 0;

    if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_XSAVE)) {
        cr4 |= 1 << 18; // CR4.OSXSAVE

        xcr0 |= 1 << 0; // XCR0.X87
        xcr0 |= 1 << 1; // XCR0.SSE
        if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_AVX)) {
            xcr0 |= 1 << 2; // XCR0.AVX
        }
        if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_AVX512F)) {
            xcr0 |= 1 << 5; // XCR0.opmask
            xcr0 |= 1 << 6; // XCR0.ZMM_Hi256
            xcr0 |= 1 << 7; // XCR0.Hi16_ZMM
        }
        arch_cr_write_cr4(cr4);
        arch_cr_write_xcr0(xcr0);
    }
}


void arch_fpu_init(uint32_t core_id) {
    if(INIT_CORE_IS_BSP(core_id)) {
        dump_fpu_info();
    }

    set_fpu_machine_state();
    if(!INIT_CORE_IS_BSP(core_id)) {
        return;
    }

    if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_XSAVE)) {
        LOG_INFO("fpu: using save method \"xsave\", fpu_size=%d\n", arch_cpuid(0x0d, 0, ARCH_CPUID_ECX));

        g_fpu_area_size = arch_cpuid(0x0d, 0, ARCH_CPUID_ECX);
        assert(g_fpu_area_size > 0);
        g_fpu_save = xsave;
        g_fpu_load = xrstor;
    } else {
        LOG_INFO("fpu: using save method: \"fxsave\", fpu_size=512\n");

        g_fpu_area_size = 512;
        g_fpu_save = fxsave;
        g_fpu_load = fxrstor;
    }
}

void* arch_fpu_alloc_area() {
    void* ptr = heap_zalloc(g_fpu_area_size);
    if(!ptr) {
        return nullptr;
    }
    assert(((uintptr_t) ptr) % 64 == 0);
    return ptr;
}

void arch_fpu_free_area(void* area) {
    heap_free(area, g_fpu_area_size);
}

size_t arch_fpu_area_size() {
    return g_fpu_area_size;
}
