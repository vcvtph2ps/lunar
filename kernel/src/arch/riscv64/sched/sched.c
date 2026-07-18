#include <common/assert.h>
#include <common/interrupts/interrupt.h>
#include <common/log.h>
#include <common/sched/process.h>
#include <common/sched/sched.h>
#include <common/sched/thread.h>
#include <lib/helpers.h>
#include <lib/string.h>
#include <lib/types.h>
#include <memory/heap.h>
#include <memory/vm.h>

[[noreturn]] void sched_arch_reset_preempt_timer() {
    assert(false && "Not implemented");
}

thread_t* sched_arch_create_kernel_thread(virt_addr_t entry) {
    (void) entry;
    assert(false && "Not implemented");
    return nullptr;
}

thread_t* sched_arch_thread_current() {
    assert(false && "Not implemented");
    return nullptr;
}

[[noreturn]] void sched_arch_context_switch(thread_t* t_current, thread_t* t_next, thread_state_t yield_state) {
    (void) t_current;
    (void) t_next;
    (void) yield_state;
    assert(false && "Not implemented");
}


#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-prototypes"
[[noreturn]] void sched_arch_init(uint32_t core_id) {
    (void) core_id;
    assert(false && "Not implemented");
}
#pragma clang diagnostic pop
