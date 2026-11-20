#include <arch/riscv64/cpu_local.h>
#include <arch/riscv64/hardware/fpu.h>
#include <arch/riscv64/hardware/sbi.h>
#include <arch/riscv64/internal/csr.h>
#include <arch/riscv64/interrupts/interrupt.h>
#include <arch/riscv64/sched/thread.h>
#include <common/assert.h>
#include <common/interrupts/dpc.h>
#include <common/interrupts/interrupt.h>
#include <common/log.h>
#include <common/sched/process.h>
#include <common/sched/sched.h>
#include <common/sched/thread.h>
#include <common/sched/timer_wait.h>
#include <common/sync/wait_obj.h>
#include <lib/helpers.h>
#include <lib/string.h>
#include <lib/types.h>
#include <memory/heap.h>
#include <memory/ptm.h>
#include <memory/vm.h>

#include "arch/riscv64/hardware/time/rdtime.h"

#define KERNEL_STACK_SIZE (16 * PAGE_SIZE_DEFAULT)

#define RISCV64_REG_SP 2
#define RISCV64_REG_A0 10
#define RISCV64_REG_A1 11

typedef struct {
    uint64_t s11, s10, s9, s8, s7, s6, s5, s4, s3, s2, s1, s0;
    virt_addr_t thread_init;
} init_context_t;

extern riscv64_thread_t* riscv64_context_switch(riscv64_thread_t* t_current, riscv64_thread_t* t_next);
extern void riscv64_kernel_thread_init();
extern void riscv64_userspace_init();

static dpc_t* g_timer_check_dpc;

void sched_arch_reset_preempt_timer();

static void timer_check_dpc(void* data) {
    (void) data;
    timer_wait_check();
}

static void sched_timer_handler(arch_interrupt_frame_t* frame, void* ctx) {
    (void) frame;
    (void) ctx;

    (void) arch_sbi_set_timer(UINT64_MAX);

    CPU_LOCAL_WRITE(scheduler.yield_pending, true);
    dpc_queue(g_timer_check_dpc);
}

void arch_thread_init_common(riscv64_thread_t* prev);

void arch_thread_init_common(riscv64_thread_t* prev) {
    sched_thread_init_common(&prev->common);
}

void sched_arch_reset_preempt_timer() {
    uint64_t frequency = CPU_LOCAL_READ(timebase_frequency);
    if(frequency == 0) {
        return;
    }

    uint64_t now = arch_rdtime_read();

    uint64_t ticks = (frequency * 10) / 1000;
    if(ticks == 0) {
        ticks = 1;
    }

    arch_sbi_ret_t ret = arch_sbi_set_timer(now + ticks);
    if(ret.error != 0) {
        LOG_WARN("Failed to program the preemption timer: %ld\n", ret.error);
    }
}

static riscv64_thread_t* sched_arch_create_thread_common(size_t tid, process_t* process, scheduler_t* sched, virt_addr_t kernel_stack_top, virt_addr_t stack) {
    riscv64_thread_t* thread = heap_zalloc(sizeof(riscv64_thread_t));
    if(thread == nullptr) {
        LOG_FAIL("Failed to allocate memory for thread object\n");
        return nullptr;
    }

    thread->fpu_area = nullptr;
    if(process != nullptr) {
        thread->fpu_area = arch_fpu_alloc_area();
    }

    thread->stack_pointer = stack;
    thread->syscall_storage = 0;
    thread->kernel_stack_top = kernel_stack_top;
    thread->common.process = process;

    thread->common.tid = tid;
    ATOMIC_STORE(&thread->common.ref_count, 1, ATOMIC_RELAXED);
    thread->common.sched.wait_entry = nullptr;
    thread->common.thread_termination_object = WAIT_OBJ_INIT_NOTIFICATION();
    ATOMIC_STORE(&thread->common.sched.migratable, true, ATOMIC_RELAXED);
    ATOMIC_STORE(&thread->common.sched.state, THREAD_STATE_READY, ATOMIC_RELAXED);
    ATOMIC_STORE(&thread->common.sched.owner, sched, ATOMIC_RELAXED);
    ATOMIC_STORE(&thread->common.exit_code, 0, ATOMIC_RELAXED);
    ATOMIC_STORE(&thread->common.in_reap_queue, false, ATOMIC_RELAXED);

    LOG_KTRC("Created thread with tid %lu\n", tid);
    return thread;
}

thread_t* sched_arch_create_kernel_thread(virt_addr_t entry) {
    virt_addr_t kernel_stack_base = (virt_addr_t) vm_map_anon(g_vm_global_address_space, VM_NO_HINT, KERNEL_STACK_SIZE, VM_PROT_RW, VM_CACHE_NORMAL, VM_FLAG_NONE);
    virt_addr_t kernel_stack_top = kernel_stack_base + KERNEL_STACK_SIZE;

    init_context_t* init_stack = (init_context_t*) (kernel_stack_top - sizeof(init_context_t));
    memory_zero(init_stack, sizeof(init_context_t));
    init_stack->s0 = entry;
    init_stack->s1 = (virt_addr_t) sched_thread_exit_kernel;
    init_stack->thread_init = (virt_addr_t) riscv64_kernel_thread_init;

    return &sched_arch_create_thread_common(thread_allocate_id(), nullptr, &CPU_LOCAL_READ(self)->scheduler, kernel_stack_top, (uintptr_t) init_stack)->common;
}


static init_context_t* sched_arch_alloc_user_init_stack(virt_addr_t kernel_stack_base, virt_addr_t kernel_stack_top, arch_interrupt_regs_t** out_frame) {
    init_context_t* context = (init_context_t*) (kernel_stack_top - sizeof(init_context_t));
    memory_zero(context, sizeof(init_context_t));

    arch_interrupt_regs_t* frame = (arch_interrupt_regs_t*) kernel_stack_base;
    memory_zero(frame, sizeof(arch_interrupt_regs_t));

    context->s0 = (virt_addr_t) frame;
    context->thread_init = (virt_addr_t) riscv64_userspace_init;

    *out_frame = frame;
    return context;
}

/// @brief Sanitizes an initial user trap frame
static void sched_arch_prepare_user_frame(arch_interrupt_regs_t* frame, virt_addr_t user_stack_top, virt_addr_t entry) {
    frame->registers[RISCV64_REG_SP] = user_stack_top;
    frame->sepc = entry;

    uint64_t sstatus = frame->sstatus;
    sstatus &= ~(ARCH_CSR_SSTATUS_SPP | ARCH_CSR_SSTATUS_SIE | ARCH_CSR_SSTATUS_SUM | ARCH_CSR_SSTATUS_FS_MASK);
    sstatus |= ARCH_CSR_SSTATUS_SPIE | ARCH_CSR_SSTATUS_FS_INITIAL;
    frame->sstatus = sstatus;
}

thread_t* sched_arch_create_thread_user(process_t* process, virt_addr_t user_stack_top, virt_addr_t entry, bool inherit_pid) {
    virt_addr_t kernel_stack_base = (virt_addr_t) vm_map_anon(g_vm_global_address_space, VM_NO_HINT, KERNEL_STACK_SIZE, VM_PROT_RW, VM_CACHE_NORMAL, VM_FLAG_NONE);
    virt_addr_t kernel_stack_top = kernel_stack_base + KERNEL_STACK_SIZE;

    arch_interrupt_regs_t* frame;
    init_context_t* context = sched_arch_alloc_user_init_stack(kernel_stack_base, kernel_stack_top, &frame);
    sched_arch_prepare_user_frame(frame, user_stack_top, entry);

    uint32_t tid = inherit_pid ? process->process_id : thread_allocate_id();
    return &sched_arch_create_thread_common(tid, process, &CPU_LOCAL_READ(self)->scheduler, kernel_stack_top, (virt_addr_t) context)->common;
}

thread_t* sched_arch_fork_thread_user(process_t* process, thread_t* parent, virt_addr_t user_stack_top, virt_addr_t entry, arch_interrupt_regs_t* frame) {
    virt_addr_t kernel_stack_base = (virt_addr_t) vm_map_anon(g_vm_global_address_space, VM_NO_HINT, KERNEL_STACK_SIZE, VM_PROT_RW, VM_CACHE_NORMAL, VM_FLAG_NONE);
    virt_addr_t kernel_stack_top = kernel_stack_base + KERNEL_STACK_SIZE;

    arch_interrupt_regs_t* child_frame;
    init_context_t* context = sched_arch_alloc_user_init_stack(kernel_stack_base, kernel_stack_top, &child_frame);
    *child_frame = *frame;
    sched_arch_prepare_user_frame(child_frame, user_stack_top, entry);
    child_frame->registers[RISCV64_REG_A0] = 0; // a0: fork returns 0 in the child
    child_frame->registers[RISCV64_REG_A1] = 0; // a1: not an error

    uint32_t tid = process->process_id;
    riscv64_thread_t* thread = sched_arch_create_thread_common(tid, process, &CPU_LOCAL_READ(self)->scheduler, kernel_stack_top, (virt_addr_t) context);

    riscv64_thread_t* arch_parent = CONTAINER_OF(parent, riscv64_thread_t, common);
    arch_fpu_save(arch_parent->fpu_area);
    memory_copy(thread->fpu_area, arch_parent->fpu_area, arch_fpu_area_size());

    return &thread->common;
}

thread_t* sched_arch_thread_current() {
    riscv64_thread_t* thread = CPU_LOCAL_GET_CURRENT_THREAD();
    assert(thread != nullptr);
    return &thread->common;
}

void sched_arch_thread_destroy(thread_t* thread) {
    riscv64_thread_t* arch_thread = CONTAINER_OF(thread, riscv64_thread_t, common);

    if(arch_thread->fpu_area != nullptr) {
        arch_fpu_free_area(arch_thread->fpu_area);
    }

    if(arch_thread->kernel_stack_top != 0) {
        virt_addr_t kernel_stack_base = arch_thread->kernel_stack_top - KERNEL_STACK_SIZE;
        vm_unmap(g_vm_global_address_space, (void*) kernel_stack_base, KERNEL_STACK_SIZE);
    }

    heap_free(arch_thread, sizeof(riscv64_thread_t));
}

void sched_arch_context_switch(thread_t* t_current, thread_t* t_next, thread_state_t yield_state) {
    LOG_KTRC("core %d, current=%u, next=%u, state=%u\n", CPU_LOCAL_READ(core_id), t_current->tid, t_next->tid, yield_state);
    riscv64_thread_t* current = CONTAINER_OF(t_current, riscv64_thread_t, common);
    riscv64_thread_t* next = CONTAINER_OF(t_next, riscv64_thread_t, common);

    CPU_LOCAL_WRITE(current_thread, next);
    interrupt_set_usermode_stack(next->kernel_stack_top);

    if(current->fpu_area != nullptr) {
        arch_fpu_save(current->fpu_area);
    }
    if(next->fpu_area != nullptr) {
        arch_fpu_load(next->fpu_area);
    }

    /// Kernel threads always run on the global address space so that a process' page tables are never left loaded once it dies
    vm_address_space_t* next_address_space = next->common.process != nullptr ? next->common.process->address_space : g_vm_global_address_space;
    ptm_load_address_space(next_address_space);

    ATOMIC_STORE(&t_current->sched.state, yield_state, ATOMIC_SEQ_CST);
    thread_state_t prev_state = ATOMIC_LOAD(&t_next->sched.state, ATOMIC_ACQUIRE);
    // These are treated as "running" states
    if(prev_state != THREAD_STATE_WAITING_IN_PROGRESS && prev_state != THREAD_STATE_WAITING_ABORTED) {
        ATOMIC_STORE(&t_next->sched.state, THREAD_STATE_RUNNING, ATOMIC_SEQ_CST);
    }

    riscv64_thread_t* prev = riscv64_context_switch(current, next);
    sched_thread_drop(&prev->common);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-prototypes"
void sched_arch_init(uint32_t core_id) {
    if(INIT_CORE_IS_BSP(core_id)) {
        interrupt_set_hardirq_handler(ARCH_INTERRUPT_CAUSE_STI, sched_timer_handler, nullptr);
        g_timer_check_dpc = dpc_create(timer_check_dpc, nullptr);
    }
}
#pragma clang diagnostic pop
