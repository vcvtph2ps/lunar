#include <arch/internal/csr.h>
#include <arch/interrupt.h>
#include <stdint.h>

[[nodiscard]] arch_interrupt_state_t arch_interrupt_get_state() {
    uint64_t val = ARCH_CSR_READ(sstatus);
    return (arch_interrupt_state_t) { .enabled = ((val & ARCH_CSR_SSTATUS_SIE) == 1) };
}

[[nodiscard]] arch_interrupt_state_t arch_interrupt_disable() {
    uint64_t val = ARCH_CSR_READ(sstatus);
    ARCH_CSR_CLEAR_BITS(sstatus, ARCH_CSR_SSTATUS_SIE);
    return (arch_interrupt_state_t) { .enabled = ((val & ARCH_CSR_SSTATUS_SIE) == 1) };
}

void arch_interrupt_restore(arch_interrupt_state_t state) {
    if(state.enabled) {
        ARCH_CSR_SET_BITS(sstatus, ARCH_CSR_SSTATUS_SIE);
    } else {
        ARCH_CSR_CLEAR_BITS(sstatus, ARCH_CSR_SSTATUS_SIE);
    }
}
