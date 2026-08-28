#include <common/sched/process.h>
#include <common/sched/sched.h>
#include <lib/helpers.h>
#include <memory/heap.h>
#include <memory/ptm.h>
#include <stdint.h>

#include "common/ldr/ldr.h"
#include "common/log.h"
#include "common/sync/spinlock.h"
#include "lib/list.h"

ATOMIC uint32_t g_next_task_id = 1;

uint32_t process_allocate_id() {
    // @todo: don't bump allocate...
    return ATOMIC_LOAD_ADD(&g_next_task_id, 1, ATOMIC_SEQ_CST);
}

void process_free_id(uint32_t id) {
    // @todo: don't bump allocate...
    (void) id;
}

process_t* process_create_from_file(const vfs_path_t* path, const ldr_process_load_info_t* load_info, thread_t** out_thread) {
    vm_address_space_t* process_address_space = heap_zalloc(sizeof(vm_address_space_t));
    ptm_init_user(process_address_space);

    size_t stack_virt_size = 1024 * PAGE_SIZE_DEFAULT;
    uintptr_t user_stack =
        (uintptr_t) vm_map_anon(process_address_space, (void*) (MEMORY_USERSPACE_END - (10 * PAGE_SIZE_DEFAULT) - stack_virt_size), stack_virt_size, VM_PROT_RW, VM_CACHE_NORMAL, VM_FLAG_FIXED | VM_FLAG_ZERO | VM_FLAG_DYNAMICALLY_BACKED);

    uintptr_t entry_point;

    bool success = ldr_setup_process(process_address_space, path, load_info, &user_stack, &entry_point);
    if(!success) {
        LOG_FAIL("process: failed to load process\n");
        // @todo: ptm_cleanup_address_space
        return nullptr;
    }

    process_t* process = heap_zalloc(sizeof(process_t));
    process->process_id = process_allocate_id();
    process->thread_list_lock = SPINLOCK_NO_DW_INIT;
    process->address_space = process_address_space;

    thread_t* thread = sched_arch_create_thread_user(process, user_stack, entry_point, true);
    assert(thread != nullptr);
    list_push(&process->thread_list, &thread->list_node_process);


    *out_thread = thread;
    return process;
}
