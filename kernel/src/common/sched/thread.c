#include <common/assert.h>
#include <common/log.h>
#include <common/sched/process.h>
#include <common/sched/sched.h>
#include <common/sched/thread.h>
#include <lib/helpers.h>

// @note: 0 is reserved, tid 1 is left for the init process' initial thread
ATOMIC static uint32_t g_next_thread_id = 2;

uint32_t thread_allocate_id() {
    // @todo: don't bump allocate...
    return ATOMIC_LOAD_ADD(&g_next_thread_id, 1, ATOMIC_SEQ_CST);
}

thread_t* thread_ref(thread_t* thread) {
    assert(thread != nullptr);
    ATOMIC_LOAD_ADD(&thread->ref_count, 1, ATOMIC_ACQ_REL);
    return thread;
}

void thread_deref(thread_t* thread) {
    assert(thread != nullptr);
    if(ATOMIC_LOAD_SUB(&thread->ref_count, 1, ATOMIC_ACQ_REL) != 1)
        return;

    /// The thread holds a reference to its process, it has to be released once the thread is gone
    process_t* process = thread->process;

    LOG_KTRC("thread %u was destroyed\n", thread->tid);
    sched_arch_thread_destroy(thread);

    if(process != nullptr)
        process_deref(process);
}
