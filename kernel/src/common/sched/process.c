#include <common/assert.h>
#include <common/fs/vfs.h>
#include <common/ldr/ldr.h>
#include <common/log.h>
#include <common/sched/process.h>
#include <common/sched/sched.h>
#include <common/sync/spinlock.h>
#include <lib/helpers.h>
#include <lib/list.h>
#include <memory/heap.h>
#include <memory/ptm.h>
#include <stdint.h>

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
    process->thread_list_lock = SPINLOCK_NO_DW_INIT;
    process->address_space = process_address_space;

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

void process_kill(process_t* process) {
    if(process->process_id == 1) {
        arch_panic("PID 1 was killed\n");
    }

    // @TODO:
    LOG_INFO("process %d was killed... unimplemented\n", process->process_id);
}
