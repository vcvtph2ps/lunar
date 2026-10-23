#include <common/assert.h>
#include <common/cpu_local.h>
#include <common/fs/vfs.h>
#include <common/interrupts/interrupt.h>
#include <common/ldr/ldr.h>
#include <common/log.h>
#include <common/sched/process.h>
#include <common/sched/process_group.h>
#include <common/sched/sched.h>
#include <common/sched/thread.h>
#include <common/sync/spinlock.h>
#include <common/sync/wait_obj.h>
#include <common/userspace/fd_store.h>
#include <lib/helpers.h>
#include <lib/list.h>
#include <lib/string.h>
#include <memory/heap.h>
#include <memory/ptm.h>
#include <memory/vm.h>
#include <stdint.h>

// @note: 0 is reserved, 1 is for the init process
ATOMIC uint32_t g_next_task_id = 2;

spinlock_t g_process_list_lock = SPINLOCK_INIT;
list_t g_process_list = LIST_INIT;

uint32_t process_allocate_id() {
    // @todo: don't bump allocate...
    return ATOMIC_LOAD_ADD(&g_next_task_id, 1, ATOMIC_SEQ_CST);
}

void process_free_id(uint32_t id) {
    // @todo: don't bump allocate...
    (void) id;
}

process_t* process_find_by_id(uint32_t id) {
    spinlock_lock(&g_process_list_lock);
    LIST_FOR_EACH(&g_process_list, node) {
        process_t* process = CONTAINER_OF(node, process_t, list_node_global);
        if(process->process_id == id) {
            process = process_try_ref(process);
            spinlock_unlock(&g_process_list_lock);
            return process;
        }
    }
    spinlock_unlock(&g_process_list_lock);
    return nullptr;
}

thread_t* process_find_thread_by_id(process_t* process, uint32_t tid) {
    thread_t* found = nullptr;

    spinlock_nodw_lock(&process->thread_list_lock);
    LIST_FOR_EACH(&process->thread_list, node) {
        thread_t* thread = CONTAINER_OF(node, thread_t, list_node_process);
        if(thread->tid == tid) {
            found = thread_ref(thread);
            break;
        }
    }
    spinlock_nodw_unlock(&process->thread_list_lock);

    return found;
}

/// @brief Links a process into the global process list used for pid lookups.
static void process_global_list_insert(process_t* process) {
    spinlock_lock(&g_process_list_lock);
    process->in_global_list = true;
    list_push(&g_process_list, &process->list_node_global);
    spinlock_unlock(&g_process_list_lock);
}

/// @brief Unlinks a process from the global process list. Safe to call if it was never linked.
static void process_global_list_remove(process_t* process) {
    spinlock_lock(&g_process_list_lock);
    if(process->in_global_list) {
        process->in_global_list = false;
        list_node_delete(&g_process_list, &process->list_node_global);
    }
    spinlock_unlock(&g_process_list_lock);
}

/// @brief Frees every resource owned by a process. Only called once its reference count reaches zero.
static void process_destroy(process_t* process) {
    process_global_list_remove(process);

    if(process->address_space != nullptr) {
        vm_address_space_destroy(process->address_space);
    }

    if(process->current_working_dir != nullptr) {
        vfs_node_put(process->current_working_dir);
    }

    if(process->fd_store != nullptr) {
        fd_store_free(process->fd_store);
    }

    process_free_id(process->process_id);
    heap_free(process, sizeof(process_t));
}

process_t* process_ref(process_t* process) {
    assert(process != nullptr);
    ATOMIC_LOAD_ADD(&process->ref_count, 1, ATOMIC_ACQ_REL);
    return process;
}

process_t* process_try_ref(process_t* process) {
    assert(process != nullptr);

    uint32_t expected = ATOMIC_LOAD(&process->ref_count, ATOMIC_ACQUIRE);
    while(expected != 0) {
        if(ATOMIC_COMPARE_EXCHANGE_WEAK(&process->ref_count, &expected, expected + 1, ATOMIC_ACQ_REL, ATOMIC_ACQUIRE))
            return process;
    }

    return nullptr;
}

void process_deref(process_t* process) {
    assert(process != nullptr);
    if(ATOMIC_LOAD_SUB(&process->ref_count, 1, ATOMIC_ACQ_REL) != 1)
        return;

    process_destroy(process);
}

static void process_init_common(process_t* process) {
    ATOMIC_STORE(&process->ref_count, 1, ATOMIC_RELAXED);
    process->in_global_list = false;
    process->children_lock = SPINLOCK_NO_DW_INIT;
    process->children_list = LIST_INIT;
    process->child_exit_event = WAIT_OBJ_INIT_SYNCHRONIZATION();
    process->thread_list_lock = SPINLOCK_NO_DW_INIT;
    process->thread_list = LIST_INIT;

    memory_zero(&process->status, sizeof(process_status_t));
    process->status.lock = SPINLOCK_NO_DW_INIT;
    process->status.state = PROCESS_RUNNING;
}

process_t* process_fork(process_t* parent) {
    process_t* process = heap_zalloc(sizeof(process_t));
    process_init_common(process);
    process->parent = parent;
    process->parent_process_id = parent->process_id;
    process->current_working_dir = vfs_node_get(parent->current_working_dir);
    process->fd_store = fd_store_clone(parent->fd_store);
    if(!process->fd_store) {
        process_deref(process);
        return nullptr;
    }

    process->address_space = vm_fork(parent->address_space);
    if(!process->address_space) {
        process_deref(process);
        return nullptr;
    }

    process->process_id = process_allocate_id();

    /// The child inherits its parent's process group, leading a new one if the parent has none
    uint32_t pgid = process_group_id(parent);
    if(!process_group_join(process, pgid != 0 ? pgid : process->process_id)) {
        process_deref(process);
        return nullptr;
    }

    /// Register with the parent so that it can wait on us. The parent's children list holds a reference
    process_ref(process);
    spinlock_nodw_lock(&parent->children_lock);
    list_push_back(&parent->children_list, &process->list_node_sibling);
    spinlock_nodw_unlock(&parent->children_lock);

    process_global_list_insert(process);

    return process;
}

process_t* process_create_from_file(vfs_node_t* node, const ldr_process_load_info_t* load_info, vfs_node_t* current_working_dir, thread_t** out_thread) {
    vm_address_space_t* process_address_space = heap_zalloc(sizeof(vm_address_space_t));
    if(!ptm_init_user(process_address_space)) {
        heap_free(process_address_space, sizeof(vm_address_space_t));
        return nullptr;
    }

    size_t stack_virt_size = 1024 * PAGE_SIZE_DEFAULT;
    uintptr_t user_stack =
        (uintptr_t) vm_map_anon(process_address_space, (void*) (MEMORY_USERSPACE_END - (10 * PAGE_SIZE_DEFAULT) - stack_virt_size), stack_virt_size, VM_PROT_RW, VM_CACHE_NORMAL, VM_FLAG_FIXED | VM_FLAG_ZERO | VM_FLAG_DYNAMICALLY_BACKED);

    uintptr_t entry_point;

    uintptr_t user_stack_top = user_stack + stack_virt_size;
    bool success = ldr_setup_process(process_address_space, node, load_info, &user_stack_top, &entry_point);
    if(!success) {
        LOG_FAIL("process: failed to load process\n");
        vm_address_space_destroy(process_address_space);
        return nullptr;
    }

    process_t* process = heap_zalloc(sizeof(process_t));
    process_init_common(process);
    process->process_id = process_allocate_id();
    process->parent_process_id = 0;
    process->parent = nullptr;
    process->address_space = process_address_space;

    // @note: nullptr is '/'
    if(current_working_dir != nullptr) {
        process->current_working_dir = vfs_node_get(current_working_dir);
    } else {
        process->current_working_dir = nullptr;
    }

    process->fd_store = fd_store_create();

    /// A process that is not forked (ie. the init process) leads its own process group
    if(!process_group_join(process, process->process_id)) {
        process_deref(process);
        return nullptr;
    }

    thread_t* thread = sched_arch_create_thread_user(process, user_stack_top, entry_point, true);
    assert(thread != nullptr);

    /// The thread references its process for as long as it lives
    process_ref(process);
    list_push(&process->thread_list, &thread->list_node_process);

    process_global_list_insert(process);

    *out_thread = thread;
    return process;
}

bool process_exec_from_file(process_t* current, vfs_node_t* node, const ldr_process_load_info_t* load_info, thread_t** out_thread) {
    vm_address_space_t* process_address_space = heap_zalloc(sizeof(vm_address_space_t));
    if(!ptm_init_user(process_address_space)) {
        heap_free(process_address_space, sizeof(vm_address_space_t));
        return false;
    }

    size_t stack_virt_size = 1024 * PAGE_SIZE_DEFAULT;
    uintptr_t user_stack =
        (uintptr_t) vm_map_anon(process_address_space, (void*) (MEMORY_USERSPACE_END - (10 * PAGE_SIZE_DEFAULT) - stack_virt_size), stack_virt_size, VM_PROT_RW, VM_CACHE_NORMAL, VM_FLAG_FIXED | VM_FLAG_ZERO | VM_FLAG_DYNAMICALLY_BACKED);

    uintptr_t entry_point;

    uintptr_t user_stack_top = user_stack + stack_virt_size;
    bool success = ldr_setup_process(process_address_space, node, load_info, &user_stack_top, &entry_point);
    if(!success) {
        LOG_FAIL("process: failed to load process\n");
        vm_address_space_destroy(process_address_space);
        return false;
    }

    thread_t* current_thread = sched_arch_thread_current();

    spinlock_nodw_lock(&current->thread_list_lock);
    size_t sibling_count = current->thread_list.count - 1;
    size_t sibling_capacity = sibling_count > 0 ? sibling_count : 1;
    wait_obj_t** thread_wait_objs = __builtin_alloca(sizeof(wait_obj_t*) * sibling_capacity);
    thread_t** sibling_threads = __builtin_alloca(sizeof(thread_t*) * sibling_capacity);
    size_t i = 0;
    LIST_FOR_EACH(&current->thread_list, thread_list_node) {
        thread_t* thread = CONTAINER_OF(thread_list_node, thread_t, list_node_process);
        if(thread != current_thread) {
            /// Take a reference while we wait on the thread, it may be retired and freed as soon as it dies
            thread_ref(thread);
            sibling_threads[i] = thread;
            thread_wait_objs[i] = &thread->thread_termination_object;
            sched_thread_kill(thread);
            i++;
        }
    }

    spinlock_nodw_unlock(&current->thread_list_lock);
    sched_wait_multiple(thread_wait_objs, sibling_count, WAIT_OBJ_WAIT_ALL, UINT64_MAX);

    for(size_t k = 0; k < sibling_count; k++) {
        thread_deref(sibling_threads[k]);
    }

    // @todo: close all cloexec fd's

    /// Swap to the new address space before taking the old one down, the current core may still have it loaded
    vm_address_space_t* old_address_space = current->address_space;
    current->address_space = process_address_space;
    ptm_load_address_space(process_address_space);

    /// The old address space is not used by anything anymore, tear it down
    vm_address_space_destroy(old_address_space);

    // @hack: this sucks so bad
    current_thread->tid = process_allocate_id();
    thread_t* thread = sched_arch_create_thread_user(current, user_stack_top, entry_point, true);
    assert(thread != nullptr);

    /// The thread references its process for as long as it lives
    process_ref(current);
    list_push(&current->thread_list, &thread->list_node_process);
    *out_thread = thread;

    return true;
}

void process_kill(process_t* process, process_status_state_t state, int32_t exit_code) {
    assert(process != nullptr);

    if(process->process_id == 1) {
        arch_panic("PID 1 was killed\n");
    }

    thread_t* current = sched_arch_thread_current();

    spinlock_nodw_lock(&process->status.lock);

    /// The process is already on its way out
    if(process->status.state != PROCESS_RUNNING) {
        spinlock_nodw_unlock(&process->status.lock);
        if(current->process == process) {
            sched_terminate_current_thread();
        }
        return;
    }

    process->status.state = state;
    switch(state) {
        case PROCESS_EXITED:     process->status.exited.exit_code = exit_code; break;
        case PROCESS_TERMINATED: process->status.terminated.signal_number = exit_code; break;
        default:                 ASSERT_UNREACHABLE();
    }
    LOG_INFO("process %u was killed\n", process->process_id);
    spinlock_nodw_unlock(&process->status.lock);

    /// Kill every thread of this process
    spinlock_nodw_lock(&process->thread_list_lock);
    LIST_FOR_EACH(&process->thread_list, node) {
        thread_t* thread = CONTAINER_OF(node, thread_t, list_node_process);

        /// The current thread is killed once the list has been walked
        if(thread == current) {
            continue;
        }

        sched_thread_kill(thread);
    }
    spinlock_nodw_unlock(&process->thread_list_lock);

    /// A killed process leaves its process group, destroying the group once it is the last member
    process_group_leave(process);

    /// The process itself no longer holds a reference. Its remaining threads, and its parent while it is a zombie, keep it alive
    process_deref(process);

    /// The current thread is owned by the process and must be killed now
    if(current->process == process) {
        sched_terminate_current_thread();
    }
}
