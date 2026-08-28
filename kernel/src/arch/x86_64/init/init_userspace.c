#include <common/arch.h>
#include <common/fs/io.h>
#include <common/fs/vfs.h>
#include <common/init.h>
#include <common/ldr/ldr.h>
#include <common/log.h>
#include <common/sched/sched.h>
#include <common/userspace/syscall.h>
#include <lib/string.h>
#include <lib/types.h>
#include <memory/heap.h>
#include <memory/ptm.h>
#include <memory/vm.h>
#include <stdint.h>

void init_stage_userspace(uint32_t core_id) {
    if(!INIT_CORE_IS_BSP(core_id)) {
        return;
    }
    syscall_init();

    ldr_process_load_info_t load_info;

    char* argv[] = { "/usr/bin/hello" };
    char* envp[] = {};

    load_info.argv = (const char**) argv;
    load_info.envp = (const char**) envp;

    load_info.argc = sizeof(argv) / sizeof(argv[0]);
    load_info.envc = sizeof(envp) / sizeof(envp[0]);

    thread_t* thread = nullptr;
    process_t* process = process_create_from_file(&VFS_MAKE_ABS_PATH("/usr/bin/hello"), &load_info, &thread);
    if(process == nullptr) {
        arch_panic("init: failed to load /usr/bin/hello\n");
    }

    sched_thread_schedule(thread);
}
