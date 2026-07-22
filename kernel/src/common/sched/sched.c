#include <common/assert.h>
#include <common/cpu_local.h>
#include <common/init.h>
#include <common/interrupts/dw.h>
#include <common/interrupts/interrupt.h>
#include <common/log.h>
#include <common/sched/sched.h>
#include <common/sched/thread.h>
#include <common/time/time.h>
#include <lib/helpers.h>

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
    thread->state = THREAD_STATE_READY;
    spinlock_lock(&thread->sched->lock);
    list_push_back(&thread->sched->thread_queue, &thread->list_node_sched);
    spinlock_unlock(&thread->sched->lock);
}

void sched_arch_init(uint32_t core_id);

void sched_init(uint32_t core_id) {
    scheduler_t* sched = &CPU_LOCAL_GET_SELF()->scheduler;
    sched->thread_queue = LIST_INIT;
    sched->lock = SPINLOCK_INIT;
    sched->idle_thread = sched_arch_create_kernel_thread((virt_addr_t) idle_thread_entry);
    sched_arch_init(core_id);
}

[[noreturn]] void sched_sleep(uint64_t msec) {
    (void) msec;
    assert(false && "unimplemented");
    // thread_t* current = sched_arch_thread_current();
    // current->sleep_until = time_monotonic_ns() + (msec * 1000000ULL);
    // sched_yield(THREAD_STATE_BLOCKED);
}

void sched_yield(thread_state_t yield_state) {
    arch_interrupt_state_t previous_state = arch_interrupt_disable();

    assert(yield_state != THREAD_STATE_RUNNING);
    assert(CPU_LOCAL_READ(scheduler.preempt_counter) == 0);
    assert(CPU_LOCAL_READ(defered_work.counter) == 0);

    thread_t* current = sched_arch_thread_current();

    thread_t* next = sched_next_thread(&CPU_LOCAL_GET_SELF()->scheduler);
    // If we have no next thread, and the current thread is ready to run, we can just continue running the current thread
    if(next == nullptr && current != current->sched->idle_thread && yield_state != THREAD_STATE_READY) next = current->sched->idle_thread;
    if(next != nullptr) {
        assert(current != next);
        sched_arch_context_switch(current, next, yield_state);
    } else {
        assert(yield_state == THREAD_STATE_READY);
    }

    sched_arch_reset_preempt_timer();
    arch_interrupt_restore(previous_state);
}

[[noreturn]] void sched_arch_handoff() {
    LOG_OKAY("core %d handing off to scheduler\n", CPU_LOCAL_READ(core_id));
    thread_t* bsp_thread = sched_arch_create_kernel_thread(0);
    bsp_thread->state = THREAD_STATE_DEAD;

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
