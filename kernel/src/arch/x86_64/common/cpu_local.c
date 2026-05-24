#include <arch/cpu_local.h>
#include <arch/msr.h>

arch_cpu_local_t* g_cpu_local_storage;

void cpu_local_init_bsp(uintptr_t cpu_local_ptr) {
    g_cpu_local_storage = (arch_cpu_local_t*) cpu_local_ptr;
    arch_cpu_local_t* cpu_local = &g_cpu_local_storage[0];
    cpu_local->core_id = 0;
}

void cpu_local_init(uint32_t core_id) {
    arch_cpu_local_t* cpu_local = &g_cpu_local_storage[core_id];
    cpu_local->core_id = core_id;
}

uint32_t arch_cpu_local_get_core_lapic_id(uint32_t core_id) {
    return g_cpu_local_storage[core_id].lapic_id;
}
