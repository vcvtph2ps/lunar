#pragma once
#if defined(__x86_64__)
#include <arch/x86_64/memory.h>
#elif defined(__riscv) && __riscv_xlen == 64
#include <arch/riscv64/memory.h>
#else
#error "Unknown architecture"
#endif
