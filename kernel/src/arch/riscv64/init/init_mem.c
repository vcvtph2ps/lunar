#include <common/assert.h>
#include <common/init.h>
#include <common/log.h>
#include <lib/helpers.h>
#include <memory/heap.h>
#include <memory/pagedb.h>
#include <memory/pmm.h>
#include <memory/ptm.h>
#include <memory/slab.h>
#include <memory/vm.h>

void init_stage_base_mem(uint32_t core_id) {
    LOG_INFO("initializing base memory on core %u\n", core_id);
    if(INIT_CORE_IS_BSP(core_id)) {
        pmm_init(core_id);
        ptm_init_kernel(core_id);
        slab_init();
        heap_init();
    } else {
        ptm_init_kernel(core_id);
    }
}
