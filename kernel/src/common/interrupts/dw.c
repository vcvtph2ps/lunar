#include <common/arch.h>
#include <common/assert.h>
#include <common/cpu_local.h>
#include <common/interrupts/dw.h>
#include <common/interrupts/interrupt.h>
#include <common/sched/sched.h>
#include <lib/list.h>
#include <memory/slab.h>

static slab_cache_t* g_dw_item_cache;

void dw_init() {
    g_dw_item_cache = slab_cache_create("dw_item_cache", sizeof(dw_item_t), alignof(dw_item_t));
}

static void cleanup_created_dw(dw_item_t* item) {
    slab_cache_free(g_dw_item_cache, item);
}

dw_item_t* dw_create(dw_function_t fn, void* data) {
    dw_item_t* item = slab_cache_alloc(g_dw_item_cache);
    item->fn = fn;
    item->data = data;
    item->cleanup_fn = cleanup_created_dw;
    ATOMIC_STORE(&item->in_use, false, ATOMIC_SEQ_CST);
    return item;
}

static bool internal_enable() {
    size_t status = CPU_LOCAL_READ(defered_work.counter);
    assertf(status > 0, "dw_internal_enable called when deferred work is already enabled");
    CPU_LOCAL_DEC(defered_work.counter);
    return status == 1;
}

void dw_queue(dw_item_t* item) {
    sched_preempt_disable();
    dw_status_disable();

    if(!ATOMIC_XCHG(&item->in_use, true, ATOMIC_SEQ_CST)) { list_push(&CPU_LOCAL_GET_SELF()->defered_work.queue, &item->list_node); }

    internal_enable();
    sched_preempt_enable();
}


void dw_process() {
    dw_status_disable();
    while(1) {
        sched_preempt_disable();
        arch_cpu_local_t* local = CPU_LOCAL_GET_SELF();
        if(local->defered_work.queue.count == 0) {
            sched_preempt_enable();
            internal_enable();
            return;
        }

        arch_interrupt_state_t prev_state = arch_interrupt_disable();
        dw_item_t* dw_item = CONTAINER_OF(list_pop_front(&local->defered_work.queue), dw_item_t, list_node);
        arch_interrupt_restore(prev_state);
        sched_preempt_enable();

        dw_item->fn(dw_item->data);
        if(dw_item->cleanup_fn) {
            dw_item->cleanup_fn(dw_item);
        } else {
            ATOMIC_STORE(&dw_item->in_use, false, ATOMIC_SEQ_CST);
        }
    }
}

void dw_status_disable() {
    assert(CPU_LOCAL_READ(defered_work.counter) < UINT32_MAX);
    CPU_LOCAL_INC(defered_work.counter);
}

void dw_status_enable() {
    if(internal_enable()) { dw_process(); }
}
