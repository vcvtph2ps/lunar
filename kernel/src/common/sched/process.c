#include <common/assert.h>
#include <common/cpu_local.h>
#include <common/fs/vfs.h>
#include <common/ldr/ldr.h>
#include <common/log.h>
#include <common/sched/process.h>
#include <common/sched/sched.h>
#include <common/sched/thread.h>
#include <common/sync/spinlock.h>
#include <common/userspace/fd_store.h>
#include <lib/helpers.h>
#include <lib/list.h>
#include <lib/string.h>
#include <memory/heap.h>
#include <memory/ptm.h>
#include <stdint.h>

#include "common/sync/wait_obj.h"

// @note: 0 is reserved, 1 is for the init process
ATOMIC uint32_t g_next_task_id = 2;

uint32_t process_allocate_id() {
    // @todo: don't bump allocate...
    return ATOMIC_LOAD_ADD(&g_next_task_id, 1, ATOMIC_SEQ_CST);
}

void process_free_id(uint32_t id) {
    // @todo: don't bump allocate...
    (void) id;
}

process_t* process_fork(process_t* parent) {
    process_t* process = heap_zalloc(sizeof(process_t));
    process->parent = parent;
    process->parent_process_id = parent->process_id;
    process->children_lock = SPINLOCK_NO_DW_INIT;
    process->children_list = LIST_INIT;
    process->child_exit_event = WAIT_OBJ_INIT(WAIT_OBJ_NOTIFICATION);
    process->current_working_dir = vfs_node_get(parent->current_working_dir);
    process->thread_list_lock = SPINLOCK_NO_DW_INIT;
    process->thread_list = LIST_INIT;
    process->fd_store = fd_store_clone(parent->fd_store);
    if(!process->fd_store) {
        heap_free(process, sizeof(process_t));
        return nullptr;
    }

    process->address_space = vm_fork(parent->address_space);
    if(!process->address_space) {
        heap_free(process, sizeof(process_t));
        fd_store_free(process->fd_store);
        return nullptr;
    }

    process->process_id = process_allocate_id();

    memory_zero(&process->status, sizeof(process_status_t));
    process->status.lock = SPINLOCK_NO_DW_INIT;
    process->status.state = PROCESS_RUNNING;

    /// Register with the parent so that it can wait on us
    spinlock_nodw_lock(&parent->children_lock);
    list_push_back(&parent->children_list, &process->list_node_sibling);
    spinlock_nodw_unlock(&parent->children_lock);

    return process;
}

process_t* process_create_from_file(const vfs_path_t* path, const ldr_process_load_info_t* load_info, vfs_node_t* current_working_dir, thread_t** out_thread) {
    vm_address_space_t* process_address_space = heap_zalloc(sizeof(vm_address_space_t));
    ptm_init_user(process_address_space);

    size_t stack_virt_size = 1024 * PAGE_SIZE_DEFAULT;
    uintptr_t user_stack =
        (uintptr_t) vm_map_anon(process_address_space, (void*) (MEMORY_USERSPACE_END - (10 * PAGE_SIZE_DEFAULT) - stack_virt_size), stack_virt_size, VM_PROT_RW, VM_CACHE_NORMAL, VM_FLAG_FIXED | VM_FLAG_ZERO | VM_FLAG_DYNAMICALLY_BACKED);

    uintptr_t entry_point;

    uintptr_t user_stack_top = user_stack + stack_virt_size;
    bool success = ldr_setup_process(process_address_space, path, load_info, &user_stack_top, &entry_point);
    if(!success) {
        LOG_FAIL("process: failed to load process\n");
        // @todo: ptm_cleanup_address_space
        return nullptr;
    }

    process_t* process = heap_zalloc(sizeof(process_t));
    process->process_id = process_allocate_id();
    process->parent_process_id = 0;
    process->parent = nullptr;
    process->children_lock = SPINLOCK_NO_DW_INIT;
    process->children_list = LIST_INIT;
    process->child_exit_event = WAIT_OBJ_INIT(WAIT_OBJ_NOTIFICATION);
    process->thread_list_lock = SPINLOCK_NO_DW_INIT;
    process->thread_list = LIST_INIT;
    process->address_space = process_address_space;

    memory_zero(&process->status, sizeof(process_status_t));
    process->status.lock = SPINLOCK_NO_DW_INIT;
    process->status.state = PROCESS_RUNNING;


    // @note: nullptr is '/'
    if(process->current_working_dir != nullptr) {
        process->current_working_dir = vfs_node_get(current_working_dir);
    } else {
        process->current_working_dir = nullptr;
    }

    process->fd_store = fd_store_create();

    thread_t* thread = sched_arch_create_thread_user(process, user_stack_top, entry_point, true);
    assert(thread != nullptr);
    list_push(&process->thread_list, &thread->list_node_process);

    *out_thread = thread;
    return process;
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

    /// The current thread is owned by the process and must be killed now
    if(current->process == process) {
        sched_terminate_current_thread();
    }
}
