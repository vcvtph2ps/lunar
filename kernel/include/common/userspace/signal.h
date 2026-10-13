#pragma once
#include <common/interrupts/interrupt.h>
#include <common/sync/spinlock.h>
#include <lib/helpers.h>
#include <lib/string.h>
#include <stdint.h>
#include <uapi/signal.h>

typedef struct process process_t; // NOLINT

static inline uapi_signal_set_t signal_mask_set(uapi_signal_set_t set, int signal_number) {
    assert(signal_number > 0 && signal_number <= UAPI_SIGNAL_MAX_SIGNAL);
    return set | ((uapi_signal_set_t) 1 << signal_number);
}

static inline uapi_signal_set_t signal_mask_clear(uapi_signal_set_t set, int signal_number) {
    assert(signal_number > 0 && signal_number <= UAPI_SIGNAL_MAX_SIGNAL);
    return set & ~((uapi_signal_set_t) 1 << signal_number);
}

/// @brief Removes the signals that can never be blocked (SIGKILL and SIGSTOP)
static inline uapi_signal_set_t signal_mask_sanitize(uapi_signal_set_t set) {
    uapi_signal_set_t forbidden = signal_mask_set(signal_mask_set(0, UAPI_SIGNAL_KILL), UAPI_SIGNAL_STOP);
    return set & ~forbidden;
}

typedef struct {
    spinlock_t lock;

    uapi_signal_set_t pending;
    uapi_signal_info_t info[UAPI_SIGNAL_MAX_SIGNAL + 1];
} signal_queue_t;

static inline bool signal_queue_can_deliver(signal_queue_t* queue, uapi_signal_set_t blocked_signals) {
    spinlock_lock(&queue->lock);
    bool can_deliver = queue->pending & ~blocked_signals;
    spinlock_unlock(&queue->lock);
    return can_deliver;
}

static inline void signal_queue_enqueue(signal_queue_t* queue, int signal_number, uapi_signal_info_t* info) {
    assert(signal_number > 0 && signal_number <= UAPI_SIGNAL_MAX_SIGNAL);

    spinlock_lock(&queue->lock);

    /// Create a set with just the signal
    uapi_signal_set_t set = signal_mask_set(0, signal_number);
    if((queue->pending & set) != 0) {
        /// Nothing to do. we have already queued it
        spinlock_unlock(&queue->lock);
        return;
    }

    queue->pending = signal_mask_set(queue->pending, signal_number);
    memory_copy(&queue->info[signal_number], info, sizeof(uapi_signal_info_t));

    spinlock_unlock(&queue->lock);
}

static inline int signal_queue_dequeue(signal_queue_t* queue, uapi_signal_set_t blocked_signals, uapi_signal_info_t* out_info) {
    spinlock_lock(&queue->lock);

    uapi_signal_set_t set = queue->pending & ~blocked_signals;
    if(set == 0) {
        spinlock_unlock(&queue->lock);
        return -1;
    }

    int signal_number = COUNT_TRAILING_ZEROS(set);
    memory_copy(out_info, &queue->info[signal_number], sizeof(uapi_signal_info_t));
    queue->pending = signal_mask_clear(queue->pending, signal_number);

    spinlock_unlock(&queue->lock);
    return signal_number;
}

/// @brief Queues a process directed signal and interrupts the first thread that does not block it
void signal_process(process_t* process, int signal_number, uapi_signal_info_t* info);

/// @brief Queues a thread directed signal on a specific thread and interrupts it
void signal_thread(thread_t* thread, int signal_number, uapi_signal_info_t* info);

/// @brief Delivers a pending signal for the current thread, if any
/// @return true if a user handler was set up on @p frame, false otherwise
bool signal_deliver_pending(arch_interrupt_frame_t* frame);
void signal_arch_setup_frame(arch_interrupt_frame_t* frame, uapi_signal_info_t* signal_info, uapi_signal_handler_t* handler);

/// @brief Returns to userspace through @p frame (which must have been set up for a handler) via iretq
[[noreturn]] void signal_arch_enter_handler(arch_interrupt_frame_t* frame);

/// @brief Restores a userspace context and returns to it
[[noreturn]] void signal_arch_restore_frame(uapi_signal_ucontext_t* context);
