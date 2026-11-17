#include <arch/x86_64/internal/asm_offsets.h>

.intel_syntax noprefix
.section .text
.globl x86_64_handle_syscall

.extern x86_64_dispatch_syscall

x86_64_handle_syscall:
    swapgs

    mov r15, qword ptr gs:[CPU_LOCAL_OFF_CURRENT_THREAD]
    mov qword ptr [r15 + X86_64_THREAD_OFF_SYSCALL_STORAGE], rsp
    mov rsp, qword ptr [r15 + X86_64_THREAD_OFF_KERNEL_STACK_TOP]

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

    mov rdi, rsp

    sti
    call x86_64_dispatch_syscall
    cli

    xor r12, r12
    mov ds, r12
    mov es, r12

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
    add rsp, 8
    pop rcx
    pop rbx
    add rsp, 8

    mov r15, qword ptr gs:[CPU_LOCAL_OFF_CURRENT_THREAD]
    mov rsp, qword ptr [r15 + X86_64_THREAD_OFF_SYSCALL_STORAGE]
    xor r15, r15

    swapgs
    sysretq
