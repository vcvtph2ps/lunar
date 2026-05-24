#include <common/init.h>
#include <common/log.h>
#include <memory/pagedb.h>
#include <memory/pmm.h>

#include "lib/helpers.h"

void init_stage_base_mem(uint32_t core_id) {
    LOG_INFO("initializing base memory on core %u\n", core_id);
    if(!INIT_CORE_IS_BSP(core_id)) { return; }
    pmm_init();

    uintptr_t a = pmm_alloc_page();
    uintptr_t b = pmm_alloc_page();
    uintptr_t c = pmm_alloc_page();
    uintptr_t d = pmm_alloc_page();
    pagedb_page_ref(a >> 12);

    LOG_OKAY("a: 0x%016lx, %lu\n", a, ATOMIC_LOAD(&pagedb_get_page(a >> 12)->ref_count, ATOMIC_RELAXED));
    LOG_OKAY("b: 0x%016lx, %lu\n", b, ATOMIC_LOAD(&pagedb_get_page(b >> 12)->ref_count, ATOMIC_RELAXED));
    LOG_OKAY("c: 0x%016lx, %lu\n", c, ATOMIC_LOAD(&pagedb_get_page(c >> 12)->ref_count, ATOMIC_RELAXED));
    LOG_OKAY("d: 0x%016lx, %lu\n", d, ATOMIC_LOAD(&pagedb_get_page(d >> 12)->ref_count, ATOMIC_RELAXED));

    pmm_free_page(a);
    pmm_free_page(a);
    pmm_free_page(b);
    pmm_free_page(c);
    pmm_free_page(d);
}
