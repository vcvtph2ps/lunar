#include <arch/cpu_local.h>
#include <common/assert.h>
#include <common/interrupts/dw.h>

static bool internal_enable() {
    size_t status = CPU_LOCAL_READ(defered_work.counter);
    assertf(status > 0, "dw_internal_enable called when deferred work is already enabled");
    CPU_LOCAL_DEC(defered_work.counter);
    return status == 1;
}

void dw_status_disable() {
    assert(CPU_LOCAL_READ(defered_work.counter) < UINT32_MAX);
    CPU_LOCAL_INC(defered_work.counter);
}

void dw_status_enable() {
    if(internal_enable()) {
        // @todo: dw_process();
    }
}
