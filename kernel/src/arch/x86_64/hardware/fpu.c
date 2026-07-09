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

// @note: the prekernel will setup the FPU for us, so we don't need to do it here
void arch_fpu_init(uint32_t core_id) {
    if(!INIT_CORE_IS_BSP(core_id)) { return; }

    if(arch_cpuid_is_feature_supported(ARCH_CPUID_FEATURE_XSAVE)) {
        g_fpu_area_size = arch_cpuid(0x0d, 0, ARCH_CPUID_ECX);
        assert(g_fpu_area_size > 0);
        g_fpu_save = xsave;
        g_fpu_load = xrstor;
    } else {
        g_fpu_area_size = 512;
        g_fpu_save = fxsave;
        g_fpu_load = fxrstor;
    }
}

void* arch_fpu_alloc_area() {
    return heap_alloc(g_fpu_area_size);
}

void arch_fpu_free_area(void* area) {
    heap_free(area, g_fpu_area_size);
}
