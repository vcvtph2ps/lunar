#pragma once
#if defined(__x86_64__)
#include <arch/x86_64/memory/pte.h>
#elif defined(__riscv) && __riscv_xlen == 64
#include <arch/riscv64/memory/pte.h>
#else
#error "Unknown architecture"
#endif
