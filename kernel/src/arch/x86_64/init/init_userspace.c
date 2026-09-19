#include <common/arch.h>
#include <common/fs/vfs.h>
#include <common/init.h>
#include <common/ldr/ldr.h>
#include <common/log.h>
#include <common/sched/sched.h>
#include <common/userspace/fd_store.h>
#include <common/userspace/syscall.h>
#include <lib/types.h>
#include <memory/ptm.h>
#include <stdint.h>

void init_stage_userspace(uint32_t core_id) {
    if(!INIT_CORE_IS_BSP(core_id)) {
        return;
    }
    syscall_init();

    ldr_process_load_info_t load_info;

    char* argv[] = { "/usr/bin/bash" };
    char* envp[] = {};

    load_info.argv = (const char**) argv;
    load_info.envp = (const char**) envp;

    load_info.argc = sizeof(argv) / sizeof(argv[0]);
    load_info.envc = sizeof(envp) / sizeof(envp[0]);

    thread_t* thread = nullptr;
    process_t* process = process_create_from_file(&VFS_MAKE_ABS_PATH("/usr/bin/bash"), &load_info, nullptr, &thread);
    if(process == nullptr) {
        arch_panic("init: failed to load /usr/bin/bash\n");
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
