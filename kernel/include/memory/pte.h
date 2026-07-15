#pragma once
#if defined(__ARCH_X86_64__)
#include <arch/x86_64/memory/pte.h>
#elif defined(__ARCH_RISCV64__)
#include <arch/riscv64/memory/pte.h>
#else
#error "Unknown architecture"
#endif
