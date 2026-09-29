#include <common/assert.h>
#include <common/log.h>
#include <common/sched/process.h>
#include <common/sched/reaper.h>
#include <common/sched/sched.h>
#include <common/sched/thread.h>
#include <common/sync/spinlock.h>
#include <common/sync/wait_obj.h>
#include <lib/helpers.h>
#include <lib/list.h>
#include <stddef.h>
#include <stdint.h>

static spinlock_no_dw_t g_reap_lock = SPINLOCK_NO_DW_INIT;
static list_t g_reap_queue = LIST_INIT;
static wait_obj_t g_reap_event = WAIT_OBJ_INIT(WAIT_OBJ_SEMAPHORE);

/// @brief Processes a single thread that the scheduler is done with
static void reaper_process_thread(thread_t* thread) {
    assert(ATOMIC_LOAD(&thread->sched.state, ATOMIC_ACQUIRE) == THREAD_STATE_TERMINATED);

    uint32_t tid = thread->tid;
    int32_t exit_code = ATOMIC_LOAD(&thread->exit_code, ATOMIC_RELAXED);
    process_t* process = thread->process;

    if(process == nullptr) {
        LOG_OKAY("reaper: processed thread %u (exit code %d)\n", tid, exit_code);
        return;
    }

    spinlock_nodw_lock(&process->thread_list_lock);
    list_node_delete(&process->thread_list, &thread->list_node_process);
    bool last_thread = process->thread_list.count == 0;
    spinlock_nodw_unlock(&process->thread_list_lock);

    LOG_OKAY("reaper: processed thread %u of process %u (exit code %d, %zu threads left)\n", tid, process->process_id, exit_code, process->thread_list.count);

    if(last_thread) {
        LOG_OKAY("reaper: process %u has no threads left\n", process->process_id);

        /// The process has fully exited, wake its parent in case it is blocked in waitpid
        process_t* parent = process->parent;
        if(parent != nullptr) {
            wait_obj_signal(&parent->child_exit_event, 1);
        }
    }
}

[[noreturn]] static void reaper_entry() {
    while(true) {
        spinlock_nodw_lock(&g_reap_lock);
        list_node_t* node = list_pop(&g_reap_queue);
        spinlock_nodw_unlock(&g_reap_lock);

        if(node == nullptr) {
            sched_wait_single(&g_reap_event, UINT64_MAX);
            continue;
        }

        reaper_process_thread(CONTAINER_OF(node, thread_t, list_node_reap_queue));
    }
}

void reaper_init() {
    thread_t* thread = sched_arch_create_kernel_thread((virt_addr_t) reaper_entry);
    assert(thread != nullptr);

    sched_thread_schedule(thread);

    LOG_OKAY("reaper: started (thread %u)\n", thread->tid);
}

void reaper_queue_thread(thread_t* thread) {
    /// A thread can be picked up for retirement from more than one place, so we need to prevent double queues
    bool expected = false;
    if(!ATOMIC_COMPARE_EXCHANGE_STRONG(&thread->in_reap_queue, &expected, true, ATOMIC_ACQ_REL, ATOMIC_ACQUIRE)) {
        return;
    }

    spinlock_nodw_lock(&g_reap_lock);
    list_push_back(&g_reap_queue, &thread->list_node_reap_queue);
    spinlock_nodw_unlock(&g_reap_lock);

    wait_obj_signal(&g_reap_event, 1);
}

size_t reaper_pending_count() {
    spinlock_nodw_lock(&g_reap_lock);
    size_t count = g_reap_queue.count;
    spinlock_nodw_unlock(&g_reap_lock);
    return count;
}
