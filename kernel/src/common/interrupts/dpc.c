#include <common/arch.h>
#include <common/assert.h>
#include <common/cpu_local.h>
#include <common/init.h>
#include <common/interrupts/dpc.h>
#include <common/interrupts/interrupt.h>
#include <common/sched/sched.h>
#include <common/sync/spinlock.h>
#include <lib/helpers.h>
#include <lib/list.h>
#include <memory/slab.h>

static slab_cache_t* g_dpc_cache;

void dpc_init(uint32_t core_id) {
    if(INIT_CORE_IS_BSP(core_id)) {
        g_dpc_cache = slab_cache_create("dpc_cache", sizeof(dpc_t), alignof(dpc_t));
    }

    CPU_LOCAL_GET_SELF()->dpc_queue.queue = LIST_INIT;
    CPU_LOCAL_GET_SELF()->dpc_queue.queue_lock = SPINLOCK_NO_INT_INIT;
}

dpc_t* dpc_create(dpc_function_t fn, void* data) {
    dpc_t* item = slab_cache_alloc(g_dpc_cache);
    item->fn = fn;
    item->data = data;
    item->target_core_id = UINT64_MAX;
    ATOMIC_STORE(&item->queue, nullptr, ATOMIC_SEQ_CST);
    return item;
}

static bool internal_enable() {
    size_t status = CPU_LOCAL_READ(dpc_queue.counter);
    assertf(status > 0, "dpc_internal_enable called when deferred work is already enabled");
    CPU_LOCAL_DEC(dpc_queue.counter);
    return status == 1;
}

bool dpc_queue(dpc_t* item) {
    sched_preempt_disable();
    dpc_disable();

    arch_cpu_local_t* cpu_local;
    if(item->target_core_id != UINT64_MAX) {
        cpu_local = cpu_local_get_other(item->target_core_id);
    } else {
        cpu_local = CPU_LOCAL_GET_SELF();
    }

    /// If we know the item is enqueued no point in trying to grab the lock of the target queue just to fail CAS
    if(ATOMIC_LOAD(&item->queue, ATOMIC_ACQUIRE) != nullptr) {
        internal_enable();
        sched_preempt_enable();
        return false;
    }

    dpc_queue_t* excepted = nullptr;
    if(ATOMIC_COMPARE_EXCHANGE_STRONG(&item->queue, &excepted, &cpu_local->dpc_queue, ATOMIC_ACQ_REL, ATOMIC_ACQUIRE)) {
        arch_interrupt_state_t state = spinlock_noint_lock(&cpu_local->dpc_queue.queue_lock);
        list_push(&cpu_local->dpc_queue.queue, &item->list_node);
        spinlock_noint_unlock(&cpu_local->dpc_queue.queue_lock, state);
    }

    internal_enable();
    sched_preempt_enable();
    return true;
}

void dpc_set_target_core(dpc_t* dpc, uint32_t target_core) {
    // @todo: assertions
    dpc->target_core_id = target_core;
}

void dpc_process() {
    sched_preempt_disable();
    dpc_disable();
    while(1) {
        arch_cpu_local_t* local = CPU_LOCAL_GET_SELF();
        if(local->dpc_queue.queue.count == 0) {
            internal_enable();
            sched_preempt_enable();
            return;
        }

        arch_interrupt_state_t state = spinlock_noint_lock(&local->dpc_queue.queue_lock);
        dpc_t* dpc = CONTAINER_OF(list_pop_front(&local->dpc_queue.queue), dpc_t, list_node);
        spinlock_noint_unlock(&local->dpc_queue.queue_lock, state);
        ATOMIC_STORE(&dpc->queue, nullptr, ATOMIC_RELEASE);

        dpc->fn(dpc->data);
    }
    internal_enable();
    sched_preempt_enable();
}

void dpc_disable() {
    assert(CPU_LOCAL_READ(dpc_queue.counter) < UINT32_MAX);
    CPU_LOCAL_INC(dpc_queue.counter);
}

void dpc_enable() {
    if(internal_enable()) {
        dpc_process();
    }
}
