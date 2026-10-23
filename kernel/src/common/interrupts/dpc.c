#include <common/arch.h>
#include <common/assert.h>
#include <common/cpu_local.h>
#include <common/interrupts/dpc.h>
#include <common/interrupts/interrupt.h>
#include <common/sched/sched.h>
#include <lib/list.h>
#include <memory/slab.h>

static slab_cache_t* g_dpc_cache;

void dpc_init() {
    g_dpc_cache = slab_cache_create("dpc_cache", sizeof(dpc_t), alignof(dpc_t));
}

static void cleanup_created_dpc(dpc_t* item) {
    slab_cache_free(g_dpc_cache, item);
}

dpc_t* dpc_create(dpc_function_t fn, void* data) {
    dpc_t* item = slab_cache_alloc(g_dpc_cache);
    item->fn = fn;
    item->data = data;
    item->cleanup_fn = cleanup_created_dpc;
    ATOMIC_STORE(&item->in_use, false, ATOMIC_SEQ_CST);
    return item;
}

static bool internal_enable() {
    size_t status = CPU_LOCAL_READ(defered_work.counter);
    assertf(status > 0, "dpc_internal_enable called when deferred work is already enabled");
    CPU_LOCAL_DEC(defered_work.counter);
    return status == 1;
}

void dpc_queue(dpc_t* item) {
    sched_preempt_disable();
    dpc_disable();

    if(!ATOMIC_XCHG(&item->in_use, true, ATOMIC_SEQ_CST)) {
        arch_interrupt_state_t prev_state = arch_interrupt_disable();
        list_push(&CPU_LOCAL_GET_SELF()->defered_work.queue, &item->list_node);
        arch_interrupt_restore(prev_state);
    }

    internal_enable();
    sched_preempt_enable();
}


void dpc_process() {
    dpc_disable();
    while(1) {
        sched_preempt_disable();
        arch_cpu_local_t* local = CPU_LOCAL_GET_SELF();
        if(local->defered_work.queue.count == 0) {
            sched_preempt_enable();
            internal_enable();
            return;
        }

        arch_interrupt_state_t prev_state = arch_interrupt_disable();
        dpc_t* dpc = CONTAINER_OF(list_pop_front(&local->defered_work.queue), dpc_t, list_node);
        arch_interrupt_restore(prev_state);
        sched_preempt_enable();

        dpc->fn(dpc->data);
        if(dpc->cleanup_fn) {
            dpc->cleanup_fn(dpc);
        } else {
            ATOMIC_STORE(&dpc->in_use, false, ATOMIC_SEQ_CST);
        }
    }
}

void dpc_disable() {
    assert(CPU_LOCAL_READ(defered_work.counter) < UINT32_MAX);
    CPU_LOCAL_INC(defered_work.counter);
}

void dpc_enable() {
    if(internal_enable()) {
        dpc_process();
    }
}
