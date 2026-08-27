#include <common/arch.h>
#include <common/fs/io.h>
#include <common/fs/vfs.h>
#include <common/init.h>
#include <common/log.h>
#include <lib/string.h>
#include <lib/types.h>
#include <memory/vm.h>
#include <stdint.h>
#include "common/ldr/bin/elf.h"
#include "memory/heap.h"
#include "memory/ptm.h"

void init_stage_userspace(uint32_t core_id) {
    if (!INIT_CORE_IS_BSP(core_id)) {
        return;
    }

    vm_address_space_t* process_address_space = heap_alloc(sizeof(vm_address_space_t));
    ptm_init_user(process_address_space);

    elf_loader_info_t elf_info;
    bool res = elf_load_file(process_address_space, &VFS_MAKE_ABS_PATH("/usr/bin/hello"), &elf_info);
    if(!res) {
        arch_panic("init: failed to load /usr/bin/hello\n");
    }

    // @todo: sched userspace thread
}
