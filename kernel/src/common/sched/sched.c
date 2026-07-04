#include <arch/cpu_local.h>
#include <common/assert.h>
#include <common/sched/sched.h>

#include "lib/helpers.h"

void sched_preempt_disable() {
    assert(CPU_LOCAL_READ(scheduler.preempt_counter) < UINT32_MAX);
    ATOMIC_LOAD_ADD(&CPU_LOCAL_GET_SELF()->scheduler.preempt_counter, 1, ATOMIC_SEQ_CST);
}

void sched_preempt_enable() {
    size_t count = CPU_LOCAL_READ(scheduler.preempt_counter);
    assert(count > 0);
    // @todo: yield
    // bool yield = count == 1 && CPU_LOCAL_EXCHANGE(preempt.yield_pending, false);
    ATOMIC_LOAD_SUB(&CPU_LOCAL_GET_SELF()->scheduler.preempt_counter, 1, ATOMIC_SEQ_CST);
    // if(yield) sched_yield(THREAD_STATE_READY);
}
