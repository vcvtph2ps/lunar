#pragma once
// NOLINTBEGIN

/* Offsets into arch_cpu_local_t */
#define CPU_LOCAL_OFF_SELF 0
#define CPU_LOCAL_OFF_CURRENT_THREAD 8
#define CPU_LOCAL_OFF_TRAP_SCRATCH 16
#define CPU_LOCAL_OFF_TRAP_SCRATCH2 24
#define CPU_LOCAL_OFF_TRAP_SCRATCH3 32

/* Offsets into riscv64_thread_t */
#define RISCV64_THREAD_OFF_STACK_POINTER 0
#define RISCV64_THREAD_OFF_SYSCALL_STORAGE 8
#define RISCV64_THREAD_OFF_KERNEL_STACK_TOP 16

// NOLINTEND

#ifndef __ASSEMBLER__

#include <arch/riscv64/cpu_local.h>
#include <arch/riscv64/sched/thread.h>
#include <stddef.h>

#define ASM_OFFSET_CHECK(constant, type, member) _Static_assert(offsetof(type, member) == (constant), "asm offset mismatch: " #constant)

ASM_OFFSET_CHECK(CPU_LOCAL_OFF_SELF, arch_cpu_local_t, self);
ASM_OFFSET_CHECK(CPU_LOCAL_OFF_CURRENT_THREAD, arch_cpu_local_t, current_thread);
ASM_OFFSET_CHECK(CPU_LOCAL_OFF_TRAP_SCRATCH, arch_cpu_local_t, trap_scratch);
ASM_OFFSET_CHECK(CPU_LOCAL_OFF_TRAP_SCRATCH2, arch_cpu_local_t, trap_scratch2);
ASM_OFFSET_CHECK(CPU_LOCAL_OFF_TRAP_SCRATCH3, arch_cpu_local_t, trap_scratch3);

ASM_OFFSET_CHECK(RISCV64_THREAD_OFF_STACK_POINTER, riscv64_thread_t, stack_pointer);
ASM_OFFSET_CHECK(RISCV64_THREAD_OFF_SYSCALL_STORAGE, riscv64_thread_t, syscall_storage);
ASM_OFFSET_CHECK(RISCV64_THREAD_OFF_KERNEL_STACK_TOP, riscv64_thread_t, kernel_stack_top);

#undef ASM_OFFSET_CHECK

#endif /* __ASSEMBLER__ */
