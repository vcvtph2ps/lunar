#pragma once
// NOLINTBEGIN

/* Offsets into arch_cpu_local_t */
#define CPU_LOCAL_OFF_SELF 0
#define CPU_LOCAL_OFF_CURRENT_THREAD 8
#define CPU_LOCAL_OFF_TRAP_SCRATCH 16
#define CPU_LOCAL_OFF_TRAP_SCRATCH2 24
#define CPU_LOCAL_OFF_TRAP_SCRATCH3 32

/* Offsets into x86_64_thread_t */
#define X86_64_THREAD_OFF_STACK_POINTER 0
#define X86_64_THREAD_OFF_SYSCALL_STORAGE 8
#define X86_64_THREAD_OFF_KERNEL_STACK_TOP 16

/* Offsets into arch_gdt_t */
#define GDT_SEGMENT_NULL 0x00
#define GDT_SEGMENT_KERNEL_CODE 0x08
#define GDT_SEGMENT_KERNEL_DATA 0x10
#define GDT_SEGMENT_USER_CODE_32 0x18
#define GDT_SEGMENT_USER_DATA 0x20
#define GDT_SEGMENT_USER_CODE 0x28

// NOLINTEND

#ifndef __ASSEMBLER__

#include <arch/x86_64/cpu_local.h>
#include <arch/x86_64/internal/gdt.h>
#include <arch/x86_64/sched/thread.h>
#include <stddef.h>

#define ASM_OFFSET_CHECK(constant, type, member) _Static_assert(offsetof(type, member) == (constant), "asm offset mismatch: " #constant)

ASM_OFFSET_CHECK(CPU_LOCAL_OFF_SELF, arch_cpu_local_t, self);
ASM_OFFSET_CHECK(CPU_LOCAL_OFF_CURRENT_THREAD, arch_cpu_local_t, current_thread);

ASM_OFFSET_CHECK(X86_64_THREAD_OFF_STACK_POINTER, x86_64_thread_t, stack_pointer);
ASM_OFFSET_CHECK(X86_64_THREAD_OFF_SYSCALL_STORAGE, x86_64_thread_t, syscall_storage);
ASM_OFFSET_CHECK(X86_64_THREAD_OFF_KERNEL_STACK_TOP, x86_64_thread_t, kernel_stack_top);

ASM_OFFSET_CHECK(X86_64_THREAD_OFF_STACK_POINTER, x86_64_thread_t, stack_pointer);
ASM_OFFSET_CHECK(X86_64_THREAD_OFF_SYSCALL_STORAGE, x86_64_thread_t, syscall_storage);
ASM_OFFSET_CHECK(X86_64_THREAD_OFF_KERNEL_STACK_TOP, x86_64_thread_t, kernel_stack_top);

ASM_OFFSET_CHECK(GDT_SEGMENT_NULL, arch_gdt_t, null);
ASM_OFFSET_CHECK(GDT_SEGMENT_KERNEL_CODE, arch_gdt_t, kernel_code);
ASM_OFFSET_CHECK(GDT_SEGMENT_KERNEL_DATA, arch_gdt_t, kernel_data);
ASM_OFFSET_CHECK(GDT_SEGMENT_USER_CODE_32, arch_gdt_t, user_code_32);
ASM_OFFSET_CHECK(GDT_SEGMENT_USER_DATA, arch_gdt_t, user_data);
ASM_OFFSET_CHECK(GDT_SEGMENT_USER_CODE, arch_gdt_t, user_code)

#undef ASM_OFFSET_CHECK

#endif /* __ASSEMBLER__ */
