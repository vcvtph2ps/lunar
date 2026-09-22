#include <arch/x86_64/cpu_local.h>
#include <arch/x86_64/hardware/fpu.h>
#include <arch/x86_64/hardware/lapic_timer.h>
#include <arch/x86_64/internal/msr.h>
#include <arch/x86_64/interrupts/interrupt.h>
#include <arch/x86_64/sched/thread.h>
#include <common/assert.h>
#include <common/interrupts/dw.h>
#include <common/interrupts/interrupt.h>
#include <common/log.h>
#include <common/sched/process.h>
#include <common/sched/sched.h>
#include <common/sched/thread.h>
#include <common/sched/timer_wait.h>
#include <common/time/time.h>
#include <lib/helpers.h>
#include <lib/string.h>
#include <lib/types.h>
#include <memory/heap.h>
#include <memory/ptm.h>
#include <memory/vm.h>

#include "common/sync/spinlock.h"

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

dw_item_t* g_timer_check_dw;

static void timer_check_dw(void* data) {
    (void) data;
    timer_wait_check();
}

static void sched_timer_handler(arch_interrupt_frame_t* frame, void* ctx) {
    (void) ctx;
    (void) frame;
    CPU_LOCAL_WRITE(scheduler.yield_pending, true);
    dw_queue(g_timer_check_dw);
}

static void arch_thread_init_common(x86_64_thread_t* prev) {
    sched_thread_init_common(&prev->common);
}


void sched_arch_reset_preempt_timer() {
    arch_lapic_timer_oneshot_ms(10);
}

static x86_64_thread_t* sched_arch_create_thread_common(size_t tid, process_t* process, scheduler_t* sched, virt_addr_t kernel_stack_top, virt_addr_t stack) {
    x86_64_thread_t* thread = heap_zalloc(sizeof(x86_64_thread_t));
    if(thread == nullptr) {
        LOG_FAIL("Failed to allocate memory for thread object\n");
        return nullptr;
    }

    thread->stack_pointer = stack;
    thread->kernel_stack_top = kernel_stack_top;
    thread->common.process = process;
    thread->fpu_area = nullptr;
    thread->fsbase = 0;
    thread->gsbase = 0;
    if(process) {
        thread->fpu_area = arch_fpu_alloc_area();
    }

    thread->common.tid = tid;

    thread->common.sched.wait_entry.thread = &thread->common;
    thread->common.sched.wait_entry.lock = SPINLOCK_INIT;

    ATOMIC_STORE(&thread->common.sched.migratable, true, ATOMIC_RELAXED);
    ATOMIC_STORE(&thread->common.sched.state, THREAD_STATE_READY, ATOMIC_RELAXED);
    ATOMIC_STORE(&thread->common.sched.owner, sched, ATOMIC_RELAXED);

    LOG_INFO("Created thread with tid %lu\n", tid);
    return thread;
}

thread_t* sched_arch_create_kernel_thread(virt_addr_t entry) {
    virt_addr_t kernel_stack_base = (virt_addr_t) vm_map_anon(g_vm_global_address_space, VM_NO_HINT, 16 * PAGE_SIZE_DEFAULT, VM_PROT_RW, VM_CACHE_NORMAL, VM_FLAG_NONE);
    virt_addr_t kernel_stack_top = kernel_stack_base + 16 * PAGE_SIZE_DEFAULT;

    init_stack_kernel_t* init_stack = (init_stack_kernel_t*) (kernel_stack_top - sizeof(init_stack_kernel_t));
    memory_zero(init_stack, sizeof(init_stack_kernel_t));
    init_stack->entry = entry;
    init_stack->thread_init = (virt_addr_t) arch_thread_init_common;
    init_stack->thread_exit = (virt_addr_t) sched_thread_exit_kernel;

    return &sched_arch_create_thread_common(process_allocate_id(), nullptr, &CPU_LOCAL_READ(self)->scheduler, kernel_stack_top, (uintptr_t) init_stack)->common;
}

thread_t* sched_arch_create_thread_user(process_t* process, virt_addr_t user_stack_top, virt_addr_t entry, bool inherit_pid) {
    virt_addr_t kernel_stack_base = (virt_addr_t) vm_map_anon(g_vm_global_address_space, VM_NO_HINT, 16 * PAGE_SIZE_DEFAULT, VM_PROT_RW, VM_CACHE_NORMAL, VM_FLAG_NONE);
    virt_addr_t kernel_stack_top = kernel_stack_base + 16 * PAGE_SIZE_DEFAULT;

    init_stack_user_t* init_stack = (init_stack_user_t*) (kernel_stack_top - sizeof(init_stack_user_t));
    memory_zero(init_stack, sizeof(init_stack_user_t));
    init_stack->entry = entry;
    init_stack->thread_init = (virt_addr_t) arch_thread_init_common;
    init_stack->thread_init_user = (virt_addr_t) x86_64_userspace_init_sysexit;
    init_stack->entry = entry;
    init_stack->user_stack = user_stack_top;

    uint32_t tid = inherit_pid ? process->process_id : process_allocate_id();
    return &sched_arch_create_thread_common(tid, process, &CPU_LOCAL_READ(self)->scheduler, kernel_stack_top, (uintptr_t) init_stack)->common;
}

thread_t* sched_arch_thread_current() {
    x86_64_thread_t* thread = CPU_LOCAL_GET_CURRENT_THREAD();
    assert(thread != nullptr);
    return &thread->common;
}

void sched_arch_context_switch(thread_t* t_current, thread_t* t_next, thread_state_t yield_state) {
    LOG_KTRC("core %d, current=%u, next=%u, state=%u\n", CPU_LOCAL_READ(core_id), t_current->tid, t_next->tid, yield_state);
    x86_64_thread_t* current = CONTAINER_OF(t_current, x86_64_thread_t, common);
    x86_64_thread_t* next = CONTAINER_OF(t_next, x86_64_thread_t, common);

    CPU_LOCAL_WRITE(current_thread, next);
    interrupt_set_usermode_stack(next->stack_pointer);

    if(current->common.process) {
        arch_fpu_save(current->fpu_area);
    }
    if(next->common.process) {
        arch_fpu_load(next->fpu_area);
    }
    if(current->common.process && next->common.process && current->common.process != next->common.process) {
        ptm_load_address_space(next->common.process->address_space);
    } else if(next->common.process) {
        ptm_load_address_space(next->common.process->address_space);
    }

    current->gsbase = arch_msr_read(ARCH_MSR_OTHER_GS_BASE);
    current->fsbase = arch_msr_read(ARCH_MSR_FS_BASE);

    arch_msr_write(ARCH_MSR_OTHER_GS_BASE, next->gsbase);
    arch_msr_write(ARCH_MSR_FS_BASE, next->fsbase);

    ATOMIC_STORE(&t_current->sched.state, yield_state, ATOMIC_SEQ_CST);
    thread_state_t prev_state = ATOMIC_LOAD(&t_next->sched.state, ATOMIC_ACQUIRE);
    // These are treated as "running" states
    if(prev_state != THREAD_STATE_WAITING_IN_PROGRESS && prev_state != THREAD_STATE_WAITING_ABORTED) {
        ATOMIC_STORE(&t_next->sched.state, THREAD_STATE_RUNNING, ATOMIC_SEQ_CST);
    }

    x86_64_thread_t* prev = x86_64_context_switch(current, next);
    sched_thread_drop(&prev->common);
}


#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-prototypes"
void sched_arch_init(uint32_t core_id) {
    if(INIT_CORE_IS_BSP(core_id)) {
        interrupt_set_hardirq_handler(LAPIC_TIMER_VECTOR, sched_timer_handler, nullptr);
        g_timer_check_dw = dw_create(timer_check_dw, nullptr);
        g_timer_check_dw->cleanup_fn = nullptr;
    }
}
#pragma clang diagnostic pop
