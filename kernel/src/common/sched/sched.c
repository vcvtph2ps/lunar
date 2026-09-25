#include <arch/x86_64/cpu_local.h>
#include <common/assert.h>
#include <common/cpu_local.h>
#include <common/init.h>
#include <common/interrupts/dw.h>
#include <common/interrupts/interrupt.h>
#include <common/log.h>
#include <common/sched/sched.h>
#include <common/sched/thread.h>
#include <common/sched/timer_wait.h>
#include <common/sync/spinlock.h>
#include <common/time/time.h>
#include <lib/helpers.h>
#include <lib/list.h>
#include <memory/heap.h>

void sched_preempt_disable() {
    assert(CPU_LOCAL_READ(scheduler.preempt_counter) < UINT32_MAX);
    ATOMIC_LOAD_ADD(&CPU_LOCAL_GET_SELF()->scheduler.preempt_counter, 1, ATOMIC_SEQ_CST);
}

void sched_preempt_enable() {
    size_t count = CPU_LOCAL_READ(scheduler.preempt_counter);
    assert(count > 0);
    bool yield = count == 1 && CPU_LOCAL_EXCHANGE(scheduler.yield_pending, false);
    ATOMIC_LOAD_SUB(&CPU_LOCAL_GET_SELF()->scheduler.preempt_counter, 1, ATOMIC_SEQ_CST);
    if(yield) sched_yield();
}

[[noreturn]] static void idle_thread_entry() {
    while(1) {
        arch_wait_for_interrupt();
    }
}

static thread_t* sched_next_thread(scheduler_t* sched) {
    spinlock_lock(&sched->lock);
    list_node_t* node = list_pop(&sched->run_queue);
    thread_t* next = node ? CONTAINER_OF(node, thread_t, sched.run_queue_node) : nullptr;
    spinlock_unlock(&sched->lock);
    return next;
}

void sched_thread_schedule(thread_t* thread) {
    scheduler_t* sched = ATOMIC_LOAD(&thread->sched.owner, ATOMIC_RELAXED);
    spinlock_lock(&sched->lock);

    thread_state_t state = ATOMIC_LOAD(&thread->sched.state, ATOMIC_ACQUIRE);
    switch(state) {
        case THREAD_STATE_DYING:      [[fallthrough]];
        case THREAD_STATE_TERMINATED: spinlock_unlock(&sched->lock); return;

        case THREAD_STATE_RUNNING:         [[fallthrough]];
        case THREAD_STATE_WAITING_ABORTED: spinlock_unlock(&sched->lock); return;

        case THREAD_STATE_READY:               [[fallthrough]];
        case THREAD_STATE_WAITING_IN_PROGRESS: [[fallthrough]];
        case THREAD_STATE_WAITING:             break;
    }

    bool expected = false;
    if(!ATOMIC_COMPARE_EXCHANGE_STRONG(&thread->sched.in_run_queue, &expected, true, ATOMIC_ACQ_REL, ATOMIC_ACQUIRE)) {
        spinlock_unlock(&sched->lock);
        return;
    }

    list_push_back(&sched->run_queue, &thread->sched.run_queue_node);
    spinlock_unlock(&sched->lock);
}

void sched_arch_init(uint32_t core_id);

void sched_init(uint32_t core_id) {
    scheduler_t* sched = &CPU_LOCAL_GET_SELF()->scheduler;
    sched->run_queue = LIST_INIT;
    sched->lock = SPINLOCK_INIT;
    sched->idle_thread = sched_arch_create_kernel_thread((virt_addr_t) idle_thread_entry);
    sched_arch_init(core_id);
}

void sched_sleep(uint64_t msec) {
    timer_wait_t* timer = timer_wait_create(msec);
    sched_wait_single(&timer->obj, UINT64_MAX);
    timer_wait_free(timer);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-prototypes"
void sched_yield_internal(thread_state_t yield_state) {
    arch_interrupt_state_t previous_state = arch_interrupt_disable();

    // @todo: figure out a cleaner way to do this, because this sucks
    assert(CPU_LOCAL_READ(scheduler.preempt_counter) == 0);
    assert(CPU_LOCAL_READ(defered_work.counter) == 0);
    assert(previous_state.enabled == true);

    thread_t* current = sched_arch_thread_current();
    thread_t* next = sched_next_thread(&CPU_LOCAL_GET_SELF()->scheduler);

    // If we have no next thread, and the current thread is ready to run, we can just continue running the current thread
    if(next == nullptr && current != CPU_LOCAL_READ(scheduler.idle_thread) && yield_state != THREAD_STATE_READY) next = CPU_LOCAL_READ(scheduler.idle_thread);
    if(next != nullptr) {
        assert(current != next);
        sched_arch_context_switch(current, next, yield_state);
    } else {
        assert(yield_state == THREAD_STATE_READY);
    }

    if(yield_state == THREAD_STATE_WAITING) {
        assert(current->sched.wait_entry != nullptr);
        spinlock_lock(&current->sched.wait_entry->lock);
        while(true) {
            list_node_t* node = list_pop(&current->sched.wait_entry->wait_blocks_list);
            if(node == nullptr) {
                break;
            }
            wait_block_t* wb = CONTAINER_OF(node, wait_block_t, thread_node);
            spinlock_nodw_lock(&wb->obj->lock);
            list_node_delete(&wb->obj->wait_blocks, &wb->wait_object_node);
            spinlock_nodw_unlock(&wb->obj->lock);
            heap_free(wb, sizeof(wait_block_t));
        }
        spinlock_unlock(&current->sched.wait_entry->lock);
    }

    sched_arch_reset_preempt_timer();
    arch_interrupt_restore(previous_state);
}
#pragma clang diagnostic pop

void sched_yield() {
    sched_yield_internal(THREAD_STATE_READY);
}

void sched_terminate() {
    sched_yield_internal(THREAD_STATE_DYING);
}

void sched_thread_drop(thread_t* thread) {
    if(thread == ATOMIC_LOAD(&thread->sched.owner, ATOMIC_RELAXED)->idle_thread) return;

    thread_state_t state = ATOMIC_LOAD(&thread->sched.state, ATOMIC_ACQUIRE);
    switch(state) {
        case THREAD_STATE_READY:
            ATOMIC_STORE(&thread->sched.in_run_queue, false, ATOMIC_RELEASE);
            sched_thread_schedule(thread);
            return;

        case THREAD_STATE_WAITING: {
            ATOMIC_STORE(&thread->sched.in_run_queue, false, ATOMIC_RELEASE);
            return;
        }

        case THREAD_STATE_DYING:
        case THREAD_STATE_TERMINATED:
            LOG_INFO("Thread %u exited, dying\n", thread->tid);
            ATOMIC_STORE(&thread->sched.state, THREAD_STATE_TERMINATED, ATOMIC_RELEASE);
            return;

        default: assertf(false, "invalid state on drop %d", state);
    }
}

void sched_thread_init_common(thread_t* prev) {
    sched_thread_drop(prev);
    (void) arch_interrupt_enable();
    sched_arch_reset_preempt_timer();
}

[[noreturn]] void sched_thread_exit_kernel() {
    sched_terminate();
    while(1);
}

[[noreturn]] void sched_arch_handoff() {
    LOG_OKAY("core %d handing off to scheduler\n", CPU_LOCAL_READ(core_id));
    thread_t* bsp_thread = sched_arch_create_kernel_thread(0);

    scheduler_t* sched = &CPU_LOCAL_READ(self)->scheduler;
    thread_t* idle_thread = sched->idle_thread;

    CPU_LOCAL_WRITE(scheduler.threaded, true);
    (void) arch_interrupt_enable();
    dw_status_enable();
    sched_preempt_enable();

    assert(CPU_LOCAL_READ(scheduler.preempt_counter) == 0);
    assert(CPU_LOCAL_READ(defered_work.counter) == 0);

    sched_arch_context_switch(bsp_thread, idle_thread, THREAD_STATE_TERMINATED);
    while(1) {
        arch_spin_hint();
    }
}
