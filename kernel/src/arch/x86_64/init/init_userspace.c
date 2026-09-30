#include <common/arch.h>
#include <common/fs/vfs.h>
#include <common/init.h>
#include <common/ldr/ldr.h>
#include <common/log.h>
#include <common/sched/sched.h>
#include <common/userspace/fd_store.h>
#include <common/userspace/syscall.h>
#include <lib/string.h>
#include <lib/types.h>
#include <memory/heap.h>
#include <memory/ptm.h>
#include <stdint.h>


#define INIT_SEARCH_LOCATIONS _("/usr/bin/init") _("/usr/bin/bash")

#define _(x) x,

static const char* g_init_search_locations[] = { INIT_SEARCH_LOCATIONS };

#undef _
#define _(x) "    " #x "\n"

static const char* g_init_search_locations_string = INIT_SEARCH_LOCATIONS;

#undef _

static bool try_load(size_t search_location, vfs_node_t* node, process_t** out_process, thread_t** out_thread) {
    ldr_process_load_info_t load_info;

    char* argv[] = { (char*) g_init_search_locations[search_location] };
    char* envp[] = {};

    load_info.argv = (const char**) argv;
    load_info.envp = (const char**) envp;

    load_info.argc = sizeof(argv) / sizeof(argv[0]);
    load_info.envc = sizeof(envp) / sizeof(envp[0]);

    thread_t* thread;

    process_t* process = process_create_from_file(node, &load_info, nullptr, &thread);
    if(process == nullptr) {
        return false;
    }

    *out_process = process;
    *out_thread = thread;

    return true;
}

void init_stage_userspace(uint32_t core_id) {
    if(!INIT_CORE_IS_BSP(core_id)) {
        return;
    }
    syscall_init();

    size_t num_init_locations = sizeof(g_init_search_locations) / sizeof(g_init_search_locations[0]);

    process_t* process = nullptr;
    thread_t* thread = nullptr;

    for(size_t i = 0; i < num_init_locations; i++) {
        vfs_node_t* node;
        vfs_result_t result = vfs_lookup(&VFS_MAKE_ABS_PATH(g_init_search_locations[i]), &node);
        if(result != VFS_RESULT_OK)
            continue;

        if(!try_load(i, node, &process, &thread)) {
            break;
        }
    }

    if(process == nullptr) {
        arch_panic("init: failed to find init process\nsearched the following paths:\n%s\n", g_init_search_locations_string);
    }


    vfs_node_t* tty_node;
    vfs_result_t result = vfs_lookup(&VFS_MAKE_ABS_PATH("/dev/tty"), &tty_node);
    if(result != VFS_RESULT_OK) {
        arch_panic("init: failed to open /dev/tty (%d)\n", result);
    }

    // @note: this is safe since we make sure pid/tid 1 is free
    process->process_id = 1;

    // Assign stdin=0, stdout=1, stderr=2 pointing at /dev/tty
    fd_store_entry_t* stdin;
    fd_store_entry_t* stdout;
    fd_store_entry_t* stderr;
    fd_store_create_fd_at(process->fd_store, tty_node, 0, &stdin);
    fd_store_create_fd_at(process->fd_store, tty_node, 1, &stdout);
    fd_store_create_fd_at(process->fd_store, tty_node, 2, &stderr);
    stdin->access.read = true;
    stdout->access.write = true;
    stderr->access.write = true;

    sched_thread_schedule(thread);
}
