#include <arch/interrupt.h>
#include <stdint.h>

[[nodiscard]] arch_interrupt_state_t arch_interrupt_disable() {
    uint64_t rflags;
    __asm__ volatile("pushfq\n" "pop %0\n" "cli\n" : "=r"(rflags) : : "memory");
    bool enabled = (rflags & (1 << 9)) > 0;
    return (arch_interrupt_state_t) { .enabled = enabled };
}

void arch_interrupt_restore(arch_interrupt_state_t state) {
    if(state.enabled) {
        __asm__ volatile("sti" ::: "memory");
    } else {
        __asm__ volatile("cli" ::: "memory");
    }
}
