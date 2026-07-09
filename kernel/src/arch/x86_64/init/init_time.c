#include <arch/x86_64/cpu_local.h>
#include <arch/x86_64/hardware/fpu.h>
#include <arch/x86_64/hardware/lapic.h>
#include <arch/x86_64/hardware/lapic_timer.h>
#include <arch/x86_64/hardware/time/kvm_pvclock.h>
#include <arch/x86_64/hardware/time/pit.h>
#include <arch/x86_64/hardware/time/tsc.h>
#include <arch/x86_64/internal/cpuid.h>
#include <arch/x86_64/internal/gdt.h>
#include <common/arch.h>
#include <common/assert.h>
#include <common/init.h>
#include <common/interrupts/dw.h>
#include <common/interrupts/interrupt.h>
#include <common/log.h>
#include <common/time/time.h>

typedef struct {
    time_timer_t* timers[16];
    time_timer_t* best_timer;
    size_t count;
} timer_registry_t;

static void register_timer(timer_registry_t* registry, time_timer_t* timer) {
    if(timer) {
        registry->timers[registry->count++] = timer;
        bool is_best = registry->best_timer == nullptr || timer->score > registry->best_timer->score;
        if(is_best) { registry->best_timer = timer; }
        LOG_OKAY("found timer: %s%s\n", timer->name, is_best ? " (best)" : "");
    }
}

void init_stage_time(uint32_t core_id) {
    timer_registry_t registry = {};

    register_timer(&registry, arch_pit_timer_get());
    register_timer(&registry, arch_kvm_pvclock_init());
    register_timer(&registry, arch_tsc_init_timer(registry.best_timer));
    arch_lapic_timer_init(core_id, registry.best_timer);

    if(INIT_CORE_IS_BSP(core_id)) { time_init(registry.best_timer); }
}
