#include <arch/cpu_local.h>

arch_cpu_local_t* g_cpu_local_storage;

void cpu_local_init_bsp(uintptr_t cpu_local_ptr) {
    g_cpu_local_storage = (arch_cpu_local_t*) cpu_local_ptr;
    arch_cpu_local_t* cpu_local = &g_cpu_local_storage[0];
    cpu_local->self = cpu_local;
    cpu_local->core_id = 0;

    // Set to 1 to prevent preemption and deferred work until the scheduler is initialized
    cpu_local->defered_work.counter = 1;
    cpu_local->preempt.counter = 1;
}

void cpu_local_init(uint32_t core_id) {
    arch_cpu_local_t* cpu_local = &g_cpu_local_storage[core_id];
    cpu_local->self = cpu_local;
    cpu_local->core_id = core_id;

    // Set to 1 to prevent preemption and deferred work until the scheduler is initialized
    cpu_local->defered_work.counter = 1;
    cpu_local->preempt.counter = 1;
}

uint32_t arch_cpu_local_get_core_hart_id(uint32_t core_id) {
    return g_cpu_local_storage[core_id].hart_id;
}
