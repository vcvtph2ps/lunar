#include <arch/cpu_local.h>
#include <common/assert.h>
#include <common/sched/sched.h>

void sched_preempt_disable() {
    assert(CPU_LOCAL_READ(preempt.counter) < UINT32_MAX);
    CPU_LOCAL_INC(preempt.counter);
}

void sched_preempt_enable() {
    size_t count = CPU_LOCAL_READ(preempt.counter);
    assert(count > 0);
    // @todo: yield
    // bool yield = count == 1 && CPU_LOCAL_EXCHANGE(preempt.yield_pending, false);
    CPU_LOCAL_DEC(preempt.counter);
    // if(yield) sched_yield(THREAD_STATE_READY);
}
