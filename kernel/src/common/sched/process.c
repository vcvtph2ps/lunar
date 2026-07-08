#include <common/sched/process.h>
#include <lib/helpers.h>
#include <stdint.h>

ATOMIC uint32_t g_next_task_id = 1;

uint32_t process_allocate_id() {
    // @todo: don't bump allocate...
    return ATOMIC_LOAD_ADD(&g_next_task_id, 1, ATOMIC_SEQ_CST);
}

void process_free_id(uint32_t id) {
    // @todo: don't bump allocate...
    (void) id;
}
