#include <arch/x86_64/cpu_local.h>
#include <arch/x86_64/hardware/time/kvm_pvclock.h>
#include <arch/x86_64/hardware/time/tsc.h>
#include <arch/x86_64/internal/cpuid.h>
#include <arch/x86_64/internal/msr.h>
#include <common/arch.h>
#include <common/cpu_local.h>
#include <common/log.h>
#include <common/sched/sched.h>
#include <common/time/time.h>
#include <memory/heap.h>
#include <memory/pmm.h>
#include <memory/ptm.h>

typedef struct [[gnu::packed]] arch_kvm_pvclock {
    uint32_t version;
    uint32_t pad0;
    uint64_t tsc_timestamp;
    uint64_t system_time;
    uint32_t tsc_to_system_mul;
    int8_t tsc_shift;
    uint8_t flags;
    uint8_t pad2[2];
} arch_kvm_pvclock_t;

static uint64_t arch_kvm_pvclock_get_ns(time_timer_t* self) {
    uint32_t version;
    uint64_t tsc, delta;
    uint64_t system_time;
    uint32_t mul;
    int8_t shift;

    arch_kvm_pvclock_t* pvclock = self->private;

    while(true) {
        version = pvclock->version;

        arch_memory_fence();

        system_time = pvclock->system_time;
        uint64_t tsc_base = pvclock->tsc_timestamp;
        mul = pvclock->tsc_to_system_mul;
        shift = pvclock->tsc_shift;

        arch_memory_fence();

        if(version == pvclock->version && !(version & 1)) {
            tsc = arch_tsc_read();
            delta = tsc - tsc_base;

            if(shift >= 0)
                delta <<= shift;
            else
                delta >>= -shift;

            uint64_t scaled = (__uint128_t) delta * mul >> 32;

            return system_time + scaled;
        }
    }
}

static uint64_t arch_kvm_pvclock_get_us(time_timer_t* self) {
    return arch_kvm_pvclock_get_ns(self) / 1000;
}

static void arch_kvm_pvclock_sleep(time_timer_t* self, uint64_t microseconds) {
    uint64_t start_ns = arch_kvm_pvclock_get_ns(self);
    uint64_t end_ns = start_ns + (microseconds * 1000);
    while(arch_kvm_pvclock_get_ns(self) < end_ns) { arch_spin_hint(); }
}

time_timer_t* arch_kvm_pvclock_init() {
    // @todo: do we need to check for kvm? couldn't other hypervisors implement this feature?
    if(arch_cpuid_get_hypervisor() != ARCH_CPUID_HYPERVISOR_KVM) return nullptr;
    if(!arch_cpuid_get_feature_value(ARCH_CPUID_FEATURE_KVM_PVCLOCK)) return nullptr;

    uint64_t paddr = pmm_alloc_page(PMM_FLAG_ZERO | PMM_FLAG_PANIC);
    arch_msr_write(ARCH_MSR_KVM_PVCLOCK, paddr | 1);

    time_timer_t* pvclock_timer = heap_alloc(sizeof(time_timer_t));
    pvclock_timer->name = "kvm_pvclock";
    pvclock_timer->score = 100;
    pvclock_timer->sleep = arch_kvm_pvclock_sleep;
    pvclock_timer->read_microseconds = arch_kvm_pvclock_get_us;
    pvclock_timer->read_raw = arch_kvm_pvclock_get_ns;
    pvclock_timer->private = (void*) PTM_TO_HHDM(paddr);

    CPU_LOCAL_WRITE(kvm_pvclock, pvclock_timer);
    return pvclock_timer;
}
