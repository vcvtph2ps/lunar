#pragma once
#include <stdint.h>

bool arch_interrupt_alloc_claim(uint8_t vector);

uint8_t arch_interrupt_alloc_allocate();
void arch_interrupt_alloc_free(uint8_t vector);
