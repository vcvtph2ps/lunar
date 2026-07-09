#include <arch/x86_64/cpu_local.h>
#include <arch/x86_64/hardware/lapic_timer.h>
#include <arch/x86_64/interrupts/interrupt.h>
#include <arch/x86_64/sched/thread.h>
#include <common/assert.h>
#include <common/interrupts/interrupt.h>
#include <common/log.h>
#include <common/sched/process.h>
#include <common/sched/sched.h>
#include <common/sched/thread.h>
#include <lib/helpers.h>
#include <lib/string.h>
#include <lib/types.h>
#include <memory/heap.h>
#include <memory/vm.h>

#define LAPIC_TIMER_VECTOR 0x20

typedef struct [[gnu::packed]] {
    uint64_t r12, r13, r14, r15, rbp, rbx;
    virt_addr_t thread_init;
    virt_addr_t entry;
    virt_addr_t thread_exit;

    uint64_t rbp_0;
    uint64_t rip_0;
} init_stack_kernel_t;

typedef struct [[gnu::packed]] {
    uint64_t r12, r13, r14, r15, rbp, rbx;
    virt_addr_t thread_init;
    virt_addr_t thread_init_user;
    virt_addr_t entry;
    virt_addr_t user_stack;
} init_stack_user_t;

extern x86_64_thread_t* x86_64_context_switch(x86_64_thread_t* t_current, x86_64_thread_t* t_next);
extern void x86_64_userspace_init_sysexit();

static void sched_timer_handler(arch_interrupt_frame_t* frame) {
    (void) frame;
    CPU_LOCAL_WRITE(scheduler.yield_pending, true);
}

static void internal_sched_thread_drop(thread_t* thread) {
    if(thread == thread->sched->idle_thread) return;

    // @todo: reap
    switch(thread->state) {
        case THREAD_STATE_READY: sched_thread_schedule(thread); break;
        case THREAD_STATE_DEAD:  {
            LOG_INFO("thread %u is dead, dropping\n", thread->tid);
            break;
        }
        case THREAD_STATE_BLOCKED: {
            wait_queue_t* queue = ATOMIC_XCHG(&sched_arch_thread_current()->target_wait_queue, nullptr, __ATOMIC_SEQ_CST);
            if(queue) { wait_queue_add_thread(queue, thread); }
            break;
        }
        default: assertf(false, "invalid state on drop %d", thread->state);
    }
}

static void internal_thread_init_common(x86_64_thread_t* prev) { // NOLINT
    internal_sched_thread_drop(&prev->common);
    (void) arch_interrupt_enable();
    sched_arch_reset_preempt_timer();
}

static void internal_thread_exit_kernel() {
    sched_yield(THREAD_STATE_DEAD);
}

void sched_arch_reset_preempt_timer() {
    arch_lapic_timer_oneshot_ms(10);
}

static x86_64_thread_t* sched_arch_create_thread_common(size_t tid, void* process, scheduler_t* sched, virt_addr_t kernel_stack_top, virt_addr_t stack) {
    (void) process;

    x86_64_thread_t* thread = heap_alloc(sizeof(x86_64_thread_t));
    thread->common.tid = tid;
    thread->common.state = THREAD_STATE_READY;
    thread->common.sched = sched;
    thread->stack_pointer = stack;
    thread->kernel_stack_top = kernel_stack_top;

    LOG_INFO("Created thread with tid %lu\n", tid);
    return thread;
}

thread_t* sched_arch_create_kernel_thread(virt_addr_t entry) {
    virt_addr_t kernel_stack_top = (virt_addr_t) vm_map_anon(g_vm_global_address_space, VM_NO_HINT, 16 * PAGE_SIZE_DEFAULT, VM_PROT_RW, VM_CACHE_NORMAL, VM_FLAG_NONE);

    init_stack_kernel_t* init_stack = (init_stack_kernel_t*) (kernel_stack_top - sizeof(init_stack_kernel_t));
    memset(init_stack, 0, sizeof(init_stack_kernel_t));
    init_stack->entry = entry;
    init_stack->thread_init = (virt_addr_t) internal_thread_init_common;
    init_stack->thread_exit = (virt_addr_t) internal_thread_exit_kernel;

    return &sched_arch_create_thread_common(process_allocate_id(), nullptr, &CPU_LOCAL_READ(self)->scheduler, kernel_stack_top, (uintptr_t) init_stack)->common;
}

thread_t* sched_arch_thread_current() {
    x86_64_thread_t* thread = CPU_LOCAL_GET_CURRENT_THREAD();
    assert(thread != nullptr);
    return &thread->common;
}

void sched_arch_context_switch(thread_t* t_current, thread_t* t_next, thread_state_t yield_state) {
    LOG_STRC("core %d, current=%u, next=%u, state=%u\n", CPU_LOCAL_READ(core_id), t_current->tid, t_next->tid, yield_state);
    x86_64_thread_t* current = CONTAINER_OF(t_current, x86_64_thread_t, common);
    x86_64_thread_t* next = CONTAINER_OF(t_next, x86_64_thread_t, common);
    CPU_LOCAL_WRITE(current_thread, next);

    t_current->state = yield_state; // @note: possible race here since we haven't *stopped* using the thread but it would be marked ready
    x86_64_thread_t* prev = x86_64_context_switch(current, next);
    internal_sched_thread_drop(&prev->common);
}


#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-prototypes"
void sched_arch_init(uint32_t core_id) {
    if(INIT_CORE_IS_BSP(core_id)) { interrupt_set_handler(LAPIC_TIMER_VECTOR, sched_timer_handler); }
}
#pragma clang diagnostic pop
