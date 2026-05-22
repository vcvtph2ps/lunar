#include <common/arch.h>
#include <common/cpu_local.h>

void arch_spin_hint() {
    __asm__ volatile("pause" ::: "memory");
}

void arch_wait_for_interrupt() {
    __asm__ volatile("hlt" ::: "memory");
}

uint64_t arch_get_core_id() {
    return CPU_LOCAL_READ(core_id);
}
