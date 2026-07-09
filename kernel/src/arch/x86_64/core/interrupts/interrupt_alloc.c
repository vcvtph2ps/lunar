#include <arch/interrupts/interrupt_alloc.h>
#include <common/sync/spinlock.h>
#include <stdint.h>

static uint8_t g_vector_map[256] = { 0 };
static spinlock_t g_vector_lock = SPINLOCK_INIT;

#define VECTOR_START 0x40
#define VECTOR_END 0xEF

bool arch_interrupt_alloc_claim(uint8_t vector) {
    spinlock_lock(&g_vector_lock);
    if(g_vector_map[vector]) {
        spinlock_unlock(&g_vector_lock);
        return false;
    }
    g_vector_map[vector] = 1;
    spinlock_unlock(&g_vector_lock);
    return vector;
}

uint8_t arch_interrupt_alloc_allocate() {
    spinlock_lock(&g_vector_lock);

    for(int v = VECTOR_START; v <= VECTOR_END; ++v) {
        uint8_t vector = v;
        if(!g_vector_map[vector]) {
            g_vector_map[vector] = 1;
            spinlock_unlock(&g_vector_lock);
            return vector;
        }
    }
    spinlock_unlock(&g_vector_lock);
    return 0;
}

void arch_interrupt_alloc_free(uint8_t vector) {
    spinlock_lock(&g_vector_lock);
    g_vector_map[vector] = 0;
    spinlock_unlock(&g_vector_lock);
}
