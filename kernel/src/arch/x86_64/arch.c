#include <common/arch.h>

void arch_spin_hint() {
    __asm__ volatile("pause" ::: "memory");
}

void arch_wait_for_interrupt() {
    __asm__ volatile("hlt" ::: "memory");
}
