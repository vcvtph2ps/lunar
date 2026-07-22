#include <arch/x86_64/cpu_local.h>
#include <arch/x86_64/hardware/lapic.h>
#include <arch/x86_64/interrupts/interrupt.h>
#include <common/arch.h>
#include <common/assert.h>
#include <common/interrupts/dw.h>
#include <common/interrupts/interrupt.h>
#include <common/log.h>
#include <common/sched/sched.h>

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

void interrupt_set_handler(uint8_t vector, interrupt_handler_fn_t handler, void* ctx) {
    assert(g_handlers[vector] == nullptr && "Interrupt handler already registered for vector");
    arch_interrupt_state_t state = spinlock_noint_lock(&g_interrupt_handler_lock);
    g_handlers[vector] = handler;
    g_handler_contexts[vector] = ctx;
    spinlock_noint_unlock(&g_interrupt_handler_lock, state);
}

extern void idt_init(uint32_t core_id);

extern void idt_set_usermode_stack(uint64_t stack_pointer);

void interrupt_init(uint32_t core_id) {
    idt_init(core_id);
}

void interrupt_set_usermode_stack(uint64_t stack_pointer) {
    idt_set_usermode_stack(stack_pointer);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-prototypes"

void x86_64_dispatch_interrupt(arch_interrupt_frame_t* frame) {
    bool is_threaded = CPU_LOCAL_READ(scheduler.threaded);
    bool is_outmost_handler = false;

    if(is_threaded) {
        is_outmost_handler = !CPU_LOCAL_GET_CURRENT_THREAD()->common.in_interrupt_handler;
        if(is_outmost_handler) { CPU_LOCAL_GET_CURRENT_THREAD()->common.in_interrupt_handler = true; }

        sched_preempt_disable();
        dw_status_disable();
    }

    if(g_handlers[frame->vector] == nullptr && frame->vector < 0x20) { arch_panic_int(frame); }
    if(g_handlers[frame->vector] != nullptr) {
        g_handlers[frame->vector](frame, g_handler_contexts[frame->vector]);
    } else {
        LOG_WARN("No handler registered for interrupt vector 0x%02lx\n", frame->vector);
    }
    if(frame->vector >= 32) arch_lapic_eoi();

    if(is_threaded) {
        (void) arch_interrupt_enable();
        dw_status_enable();
        (void) arch_interrupt_disable();

        sched_preempt_enable();

        if(is_outmost_handler) { CPU_LOCAL_GET_CURRENT_THREAD()->common.in_interrupt_handler = false; }
    }
}

#pragma clang diagnostic pop
