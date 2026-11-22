#include <arch/x86_64/cpu_local.h>
#include <arch/x86_64/hardware/lapic.h>
#include <arch/x86_64/interrupts/interrupt.h>
#include <common/arch.h>
#include <common/assert.h>
#include <common/interrupts/dpc.h>
#include <common/interrupts/interrupt.h>
#include <common/log.h>
#include <common/sched/sched.h>
#include <common/sync/wait_obj.h>
#include <memory/heap.h>

[[nodiscard]] arch_interrupt_state_t arch_interrupt_get_state() {
    uint64_t rflags;
    __asm__ volatile("pushfq\n" "pop %0\n" : "=r"(rflags) : : "memory");
    bool enabled = (rflags & (1 << 9)) > 0;
    return (arch_interrupt_state_t) { .enabled = enabled };
}

[[nodiscard]] arch_interrupt_state_t arch_interrupt_disable() {
    uint64_t rflags;
    __asm__ volatile("pushfq\n" "pop %0\n" "cli\n" : "=r"(rflags) : : "memory");
    bool enabled = (rflags & (1 << 9)) > 0;
    return (arch_interrupt_state_t) { .enabled = enabled };
}

[[nodiscard]] arch_interrupt_state_t arch_interrupt_enable() {
    uint64_t rflags;
    __asm__ volatile("pushfq\n" "pop %0\n" "sti\n" : "=r"(rflags) : : "memory");
    bool enabled = (rflags & (1 << 9)) > 0;
    return (arch_interrupt_state_t) { .enabled = enabled };
}

void arch_interrupt_restore(arch_interrupt_state_t state) {
    if(state.enabled) {
        __asm__ volatile("sti" ::: "memory");
    } else {
        __asm__ volatile("cli" ::: "memory");
    }
}

spinlock_no_int_t g_interrupt_handler_lock = SPINLOCK_NO_INT_INIT;
interrupt_handler_fn_t g_handlers[256] = {};
void* g_handler_contexts[256] = {};

void interrupt_set_hardirq_handler(uint8_t vector, interrupt_handler_fn_t handler, void* ctx) {
    arch_interrupt_state_t state = spinlock_noint_lock(&g_interrupt_handler_lock);
    assert(g_handlers[vector] == nullptr && "Interrupt handler already registered for vector");
    g_handlers[vector] = handler;
    g_handler_contexts[vector] = ctx;
    spinlock_noint_unlock(&g_interrupt_handler_lock, state);
}

static void softirq_handler(arch_interrupt_frame_t* frame, void* ctx) {
    (void) frame;
    dpc_queue(ctx);
}

void interrupt_set_softirq_handler(uint8_t vector, dpc_t* dpc) {
    arch_interrupt_state_t state = spinlock_noint_lock(&g_interrupt_handler_lock);
    assert(g_handlers[vector] == nullptr && "Interrupt handler already registered for vector");
    g_handlers[vector] = softirq_handler;
    g_handler_contexts[vector] = dpc;
    spinlock_noint_unlock(&g_interrupt_handler_lock, state);
}

static void signal_wake_obj_handler_dpc(void* ctx) {
    wait_obj_signal(ctx, 1);
}

void interrupt_set_waitobj_handler(uint8_t vector, wait_obj_t* wait_obj) {
    if(wait_obj->type == WAIT_OBJ_SEMAPHORE) {
        ASSERT_TODO();
    }

    dpc_t* item = dpc_create(signal_wake_obj_handler_dpc, wait_obj);
    interrupt_set_softirq_handler(vector, item);
}

extern void idt_init(uint32_t core_id);

extern void idt_set_usermode_stack(uint64_t stack_pointer);

void interrupt_init(uint32_t core_id) {
    idt_init(core_id);
}

void interrupt_set_usermode_stack(uint64_t stack_pointer) {
    idt_set_usermode_stack(stack_pointer);
}

/// @brief Convert exception cause to the signal it should raise
/// @return The signal number, or -1 if the exception is fatal
static int exception_vector_to_signal(uint64_t vector) {
    switch(vector) {
        case 0x00: return UAPI_SIGNAL_FPE; // #DE divide error
        case 0x01: return UAPI_SIGNAL_TRAP; // #DB debug exception
        case 0x03: return UAPI_SIGNAL_TRAP; // #BP breakpoint
        case 0x04: return UAPI_SIGNAL_SEGV; // #OF overflow
        case 0x05: return UAPI_SIGNAL_SEGV; // #BR bound range exceeded
        case 0x06: return UAPI_SIGNAL_ILL; // #UD undefined opcode
        case 0x07: return UAPI_SIGNAL_ILL; // #NM device not available
        case 0x09: return UAPI_SIGNAL_FPE; // coprocessor segment overrun
        case 0x0A: return UAPI_SIGNAL_SEGV; // #TS invalid TSS
        case 0x0B: return UAPI_SIGNAL_BUS; // #NP segment not present
        case 0x0C: return UAPI_SIGNAL_BUS; // #SS stack segment fault
        case 0x0D: return UAPI_SIGNAL_SEGV; // #GP general protection fault
        case 0x10: return UAPI_SIGNAL_FPE; // #MF x87 floating point error
        case 0x11: return UAPI_SIGNAL_BUS; // #AC alignment check
        case 0x13: return UAPI_SIGNAL_FPE; // #XM SIMD floating point exception
        case 0x14: return UAPI_SIGNAL_SEGV; // #VE virtualization exception
        case 0x15: return UAPI_SIGNAL_SEGV; // #CP control protection exception
        default:   return -1; // #NMI, #DF, #MC and friends are unrecoverable
    }
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-prototypes"

void handle_exception(arch_interrupt_frame_t* frame) {
    if(!frame->is_user) {
        arch_panic_int(frame);
    }

    int signal_number = exception_vector_to_signal(frame->vector);
    if(signal_number < 0) {
        arch_panic_int(frame);
    }

    thread_t* thread = &CPU_LOCAL_GET_CURRENT_THREAD()->common;
    uapi_signal_info_t info = { .signal_number = signal_number, .code = UAPI_SIGNAL_KERNEL, .addr = (void*) frame->state->rip };
    signal_thread(thread, signal_number, &info);
}

void x86_64_dispatch_interrupt(arch_interrupt_frame_t* frame) {
    bool is_threaded = CPU_LOCAL_READ(scheduler.threaded);
    bool is_outmost_handler = false;

    if(is_threaded) {
        is_outmost_handler = !CPU_LOCAL_GET_CURRENT_THREAD()->common.in_interrupt_handler;
        if(is_outmost_handler) {
            CPU_LOCAL_GET_CURRENT_THREAD()->common.in_interrupt_handler = true;
        }

        sched_preempt_disable();
        dpc_disable();
    }

    CPU_LOCAL_WRITE(in_hardirq, true);
    if(g_handlers[frame->vector] == nullptr && frame->vector < 0x20) {
        handle_exception(frame);
    } else if(g_handlers[frame->vector] != nullptr) {
        g_handlers[frame->vector](frame, g_handler_contexts[frame->vector]);
    } else {
        LOG_WARN("No handler registered for interrupt vector 0x%02lx\n", frame->vector);
    }

    if(frame->vector >= 32)
        arch_lapic_eoi();

    CPU_LOCAL_WRITE(in_hardirq, false);

    if(is_threaded) {
        CPU_LOCAL_WRITE(in_softirq, false);
        (void) arch_interrupt_enable();
        dpc_enable();
        CPU_LOCAL_WRITE(in_softirq, false);

        sched_preempt_enable();

        if(is_outmost_handler) {
            CPU_LOCAL_GET_CURRENT_THREAD()->common.in_interrupt_handler = false;
        }

        if(frame->is_user) {
            (void) signal_userspace_prepare_return(frame);
        }
    }
}

#pragma clang diagnostic pop
