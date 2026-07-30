#include <common/assert.h>
#include <common/cpu_local.h>
#include <common/init.h>
#include <common/interrupts/dw.h>
#include <common/interrupts/interrupt.h>
#include <common/log.h>
#include <common/sched/sched.h>
#include <common/sched/sleep_queue.h>
#include <common/sched/thread.h>
#include <common/time/time.h>
#include <lib/helpers.h>

sleep_queue_t g_sched_sleep_queue = { .lock = SPINLOCK_NO_DW_INIT, .queue = LIST_INIT };

void sched_preempt_disable() {
    assert(CPU_LOCAL_READ(scheduler.preempt_counter) < UINT32_MAX);
    ATOMIC_LOAD_ADD(&CPU_LOCAL_GET_SELF()->scheduler.preempt_counter, 1, ATOMIC_SEQ_CST);
}

void sched_preempt_enable() {
    size_t count = CPU_LOCAL_READ(scheduler.preempt_counter);
    assert(count > 0);
    bool yield = count == 1 && CPU_LOCAL_EXCHANGE(scheduler.yield_pending, false);
    ATOMIC_LOAD_SUB(&CPU_LOCAL_GET_SELF()->scheduler.preempt_counter, 1, ATOMIC_SEQ_CST);
    if(yield) sched_yield(THREAD_STATE_READY);
}

[[noreturn]] static void idle_thread_entry() {
    while(1) { arch_wait_for_interrupt(); }
}

static thread_t* sched_next_thread(scheduler_t* sched) {
    spinlock_lock(&sched->lock);
    list_node_t* node = list_pop(&sched->thread_queue);
    thread_t* next = node ? CONTAINER_OF(node, thread_t, list_node_sched) : nullptr;
    spinlock_unlock(&sched->lock);
    return next;
}

void sched_thread_schedule(thread_t* thread) {
    scheduler_t* sched = ATOMIC_LOAD(&thread->sched, ATOMIC_RELAXED);
    spinlock_lock(&sched->lock);

    thread_state_t state = ATOMIC_LOAD(&thread->current_state, ATOMIC_ACQUIRE);
    switch(state) {
        case THREAD_STATE_DYING:
        case THREAD_STATE_DEAD:  spinlock_unlock(&sched->lock); return;
        case THREAD_STATE_RUNNING:
            ATOMIC_STORE(&thread->wake_pending, true, ATOMIC_RELEASE);
            spinlock_unlock(&sched->lock);
            return;
        case THREAD_STATE_READY:   [[fallthrough]];
        case THREAD_STATE_BLOCKED: break;
    }

    bool expected = false;
    if(!ATOMIC_COMPARE_EXCHANGE_STRONG(&thread->in_run_queue, &expected, true, ATOMIC_ACQ_REL, ATOMIC_ACQUIRE)) {
        spinlock_unlock(&sched->lock);
        return;
    }

    if(state == THREAD_STATE_BLOCKED) {
        thread_state_t expected = THREAD_STATE_BLOCKED;
        if(!ATOMIC_COMPARE_EXCHANGE_STRONG(&thread->current_state, &expected, THREAD_STATE_READY, ATOMIC_ACQ_REL, ATOMIC_ACQUIRE)) {
            ATOMIC_STORE(&thread->in_run_queue, false, ATOMIC_RELEASE);
            spinlock_unlock(&sched->lock);
            return;
        }

        if(ATOMIC_LOAD(&thread->sleep_until, ATOMIC_RELAXED) != 0) {
            ATOMIC_STORE(&thread->sleep_until, 0, ATOMIC_RELAXED);
            spinlock_nodw_lock(&g_sched_sleep_queue.lock);
            list_node_delete(&g_sched_sleep_queue.queue, &thread->list_node_sleep_queue);
            spinlock_nodw_unlock(&g_sched_sleep_queue.lock);
        }

        wait_queue_t* wq = ATOMIC_XCHG(&thread->current_wait_queue, nullptr, ATOMIC_ACQ_REL);
        if(wq) {
            spinlock_nodw_lock(&wq->lock);
            list_node_delete(&wq->list, &thread->list_node_wait);
            spinlock_nodw_unlock(&wq->lock);
        }

        thread->target_wait_queue = nullptr;
    }

    list_push_back(&sched->thread_queue, &thread->list_node_sched);
    spinlock_unlock(&sched->lock);
}

void sched_arch_init(uint32_t core_id);

void sched_init(uint32_t core_id) {
    scheduler_t* sched = &CPU_LOCAL_GET_SELF()->scheduler;
    sched->thread_queue = LIST_INIT;
    sched->lock = SPINLOCK_INIT;
    sched->idle_thread = sched_arch_create_kernel_thread((virt_addr_t) idle_thread_entry);
    sched_arch_init(core_id);
}

void sched_sleep(uint64_t msec) {
    thread_t* current = sched_arch_thread_current();
    current->sleep_until = time_monotonic_ns() + (msec * 1000000ULL);
    sched_yield(THREAD_STATE_BLOCKED);
}

void sched_yield(thread_state_t yield_state) {
    arch_interrupt_state_t previous_state = arch_interrupt_disable();

    assert(yield_state != THREAD_STATE_RUNNING);
    assert(CPU_LOCAL_READ(scheduler.preempt_counter) == 0);
    assert(CPU_LOCAL_READ(defered_work.counter) == 0);

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

    sched_arch_reset_preempt_timer();
    arch_interrupt_restore(previous_state);
}

void sched_thread_drop(thread_t* thread) {
    if(thread == ATOMIC_LOAD(&thread->sched, ATOMIC_RELAXED)->idle_thread) return;

    thread_state_t state = ATOMIC_LOAD(&thread->current_state, ATOMIC_ACQUIRE);
    switch(state) {
        case THREAD_STATE_READY:
            ATOMIC_STORE(&thread->in_run_queue, false, ATOMIC_RELEASE);
            sched_thread_schedule(thread);
            return;

        case THREAD_STATE_BLOCKED: {
            ATOMIC_STORE(&thread->in_run_queue, false, ATOMIC_RELEASE);

            if(ATOMIC_XCHG(&thread->wake_pending, false, ATOMIC_ACQ_REL)) {
                sched_thread_schedule(thread);
                return;
            }

            wait_queue_t* wq = ATOMIC_XCHG(&thread->target_wait_queue, nullptr, ATOMIC_ACQ_REL);
            if(wq) { wait_queue_add_thread(wq, thread); }

            if(ATOMIC_LOAD(&thread->sleep_until, ATOMIC_RELAXED) != 0) { sleep_queue_insert(&g_sched_sleep_queue, thread); }
            return;
        }

        case THREAD_STATE_DYING:
            LOG_INFO("Thread %u exited, dying\n", thread->tid);
            ATOMIC_STORE(&thread->current_state, THREAD_STATE_DEAD, ATOMIC_RELEASE);
            return;

        case THREAD_STATE_DEAD: return;

        default: assertf(false, "invalid state on drop %d", state);
    }
}

void sched_thread_init_common(thread_t* prev) {
    sched_thread_drop(prev);
    (void) arch_interrupt_enable();
    sched_arch_reset_preempt_timer();
}

[[noreturn]] void sched_thread_exit_kernel() {
    sched_yield(THREAD_STATE_DEAD);
    while(1);
}

[[noreturn]] void sched_arch_handoff() {
    LOG_OKAY("core %d handing off to scheduler\n", CPU_LOCAL_READ(core_id));
    thread_t* bsp_thread = sched_arch_create_kernel_thread(0);
    ATOMIC_STORE(&bsp_thread->current_state, THREAD_STATE_DEAD, ATOMIC_SEQ_CST);

    scheduler_t* sched = &CPU_LOCAL_READ(self)->scheduler;
    thread_t* idle_thread = sched->idle_thread;

    CPU_LOCAL_WRITE(scheduler.threaded, true);
    (void) arch_interrupt_enable();
    dw_status_enable();
    sched_preempt_enable();

    assert(CPU_LOCAL_READ(scheduler.preempt_counter) == 0);
    assert(CPU_LOCAL_READ(defered_work.counter) == 0);

    sched_arch_context_switch(bsp_thread, idle_thread, THREAD_STATE_DEAD);
    while(1) { arch_spin_hint(); }
}
