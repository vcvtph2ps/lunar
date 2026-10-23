#include <common/assert.h>
#include <common/cpu_local.h>
#include <common/interrupts/interrupt.h>
#include <common/log.h>
#include <common/sched/process.h>
#include <common/sched/sched.h>
#include <common/sched/thread.h>
#include <common/userspace/signal.h>
#include <lib/helpers.h>
#include <lib/list.h>

/// @brief Applies the process wide default action for a signal that has no handler
static void signal_apply_default_action(process_t* process, int signal_number) {
    switch(uapi_sig_default_action(signal_number)) {
        case UAPI_SIGNAL_DEFAULT_ACTION_IGNORE:
        case UAPI_SIGNAL_DEFAULT_ACTION_CONTINUE: return;

        case UAPI_SIGNAL_DEFAULT_ACTION_STOP:
            /// @todo: actually stop the process
            return;

        case UAPI_SIGNAL_DEFAULT_ACTION_TERMINATE:
        case UAPI_SIGNAL_DEFAULT_ACTION_CORE_DUMP: process_kill(process, PROCESS_TERMINATED, signal_number); return;
    }

    ASSERT_UNREACHABLE();
}

void signal_process(process_t* process, int signal_number, uapi_signal_info_t* info) {
    signal_queue_enqueue(&process->signal_queue, signal_number, info);

    /// A process directed signal is delivered to a single thread
    /// we pick first one that does not block it
    uapi_signal_set_t signal_set = signal_mask_set(0, signal_number);
    spinlock_no_dpc_lock(&process->thread_list_lock);
    LIST_FOR_EACH(&process->thread_list, node) {
        thread_t* thread = CONTAINER_OF(node, thread_t, list_node_process);

        spinlock_lock(&thread->signal.lock);
        bool blocked = (thread->signal.masked_signal_set & signal_set) != 0;
        spinlock_unlock(&thread->signal.lock);

        if(blocked) {
            continue;
        }

        sched_thread_interrupt(thread);
        break;
    }
    spinlock_no_dpc_unlock(&process->thread_list_lock);
}

void signal_thread(thread_t* thread, int signal_number, uapi_signal_info_t* info) {
    signal_queue_enqueue(&thread->signal_queue, signal_number, info);
    sched_thread_interrupt(thread);
}

bool signal_deliver_pending(arch_interrupt_frame_t* frame) {
    thread_t* current_thread = &CPU_LOCAL_GET_CURRENT_THREAD()->common;
    process_t* process = current_thread->process;

    uapi_signal_info_t signal_info;
    uapi_signal_set_t blocked = current_thread->signal.masked_signal_set;

    int signal_number = signal_queue_dequeue(&current_thread->signal_queue, blocked, &signal_info);
    if(signal_number == -1) {
        signal_number = signal_queue_dequeue(&process->signal_queue, blocked, &signal_info);
        if(signal_number == -1) {
            return false;
        }
    }

    /// The signal is being delivered, any wait that was interrupted has already backed out
    ATOMIC_STORE(&current_thread->interrupted, false, ATOMIC_RELAXED);

    uapi_signal_handler_t handler;
    spinlock_lock(&process->signal_handler.lock);
    handler = process->signal_handler.handlers[signal_number];
    spinlock_unlock(&process->signal_handler.lock);

    if(handler.handler == (uintptr_t) UAPI_SIGNAL_IGNORE) {
        return false;
    }

    /// A zero handler means the default action, as does the explicit UAPI_SIGNAL_DEFAULT
    if(handler.handler == 0 || handler.handler == (uintptr_t) UAPI_SIGNAL_DEFAULT) {
        signal_apply_default_action(process, signal_number);
        return false;
    }

    signal_arch_setup_frame(frame, &signal_info, &handler);

    /// the signal being handled is blocked for the duration of the handler unless SA_NODEFER is set, along with every signal in the handler's mask. sigreturn restores the previous mask.
    spinlock_lock(&current_thread->signal.lock);
    uapi_signal_set_t new_mask = current_thread->signal.masked_signal_set;
    if(!(handler.flags & UAPI_SIGNAL_FLAG_NODEFER)) {
        new_mask = signal_mask_set(new_mask, signal_number);
    }
    new_mask |= signal_mask_sanitize(handler.mask);
    current_thread->signal.masked_signal_set = new_mask;
    spinlock_unlock(&current_thread->signal.lock);

    return true;
}
