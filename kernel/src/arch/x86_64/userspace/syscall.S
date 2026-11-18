#include <arch/x86_64/internal/asm_offsets.h>

.intel_syntax noprefix
.section .text

.extern x86_64_dispatch_syscall
.globl x86_64_handle_syscall
.type x86_64_handle_syscall, @function

/// @brief Entry point of the `syscall` instruction
x86_64_handle_syscall:
    /// @safety: When designing syscall, AMD decided that atomic stack switching and swapgs were not needed
    /// this leaves us with the problem that if we take an nmi *right* before swapgs, we'll have taken that nmi "in" kernel mode while having a userspace gs base loaded
    /// this will cause many issues, and we can only deal with it by checking in the nmi handler
    // swap to the kernel gs base so the cpu local block can be reached
    swapgs

    // switch to this thread's kernel stack, stashing the userspace rsp
    mov r15, qword ptr gs:[CPU_LOCAL_OFF_CURRENT_THREAD]
    mov qword ptr [r15 + X86_64_THREAD_OFF_SYSCALL_STORAGE], rsp
    mov rsp, qword ptr [r15 + X86_64_THREAD_OFF_KERNEL_STACK_TOP]

    // build syscall frame, the userspace rsp is pushed last so it sits at the base
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    push qword ptr [r15 + X86_64_THREAD_OFF_SYSCALL_STORAGE]

    // pass the frame as the first argument
    mov rdi, rsp

    // call with interrupts enabled
    sti
    call x86_64_dispatch_syscall
    cli

    // clear the user data segments so they get reloaded
    xor r12, r12
    mov ds, r12
    mov es, r12

    // skip saved userspace rsp
    add rsp, 8
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    add rsp, 8 // skip rdx (return value)
    pop rcx
    pop rbx
    add rsp, 8 // skip rax, (return value)

    // restore userspace stack
    mov r15, qword ptr gs:[CPU_LOCAL_OFF_CURRENT_THREAD]
    mov rsp, qword ptr [r15 + X86_64_THREAD_OFF_SYSCALL_STORAGE]
    xor r15, r15

    swapgs
    sysretq
.size x86_64_handle_syscall, . - x86_64_handle_syscall
