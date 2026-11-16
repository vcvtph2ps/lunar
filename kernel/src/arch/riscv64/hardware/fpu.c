#include <arch/riscv64/hardware/fpu.h>
#include <arch/riscv64/internal/csr.h>
#include <common/assert.h>
#include <common/log.h>
#include <memory/heap.h>

typedef struct {
    uint64_t registers[32];
    uint64_t fcsr;
} riscv64_fpu_state_t;

static_assert(sizeof(riscv64_fpu_state_t) == 264);

void arch_fpu_init(uint32_t core_id) {
    (void) core_id;
    ARCH_CSR_SET_BITS(sstatus, ARCH_CSR_SSTATUS_FS_INITIAL);
}

void* arch_fpu_alloc_area() {
    void* ptr = heap_zalloc(arch_fpu_area_size());
    assert(ptr != nullptr);
    assert(((uintptr_t) ptr) % 8 == 0);
    return ptr;
}

void arch_fpu_free_area(void* area) {
    heap_free(area, arch_fpu_area_size());
}

size_t arch_fpu_area_size() {
    return sizeof(riscv64_fpu_state_t);
}
