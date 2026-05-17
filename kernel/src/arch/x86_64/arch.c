#include <common/arch.h>

void arch_relax() {
    __asm__ volatile("pause" ::: "memory");
}
