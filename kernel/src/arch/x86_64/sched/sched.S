.intel_syntax noprefix
#include <arch/x86_64/internal/asm_offsets.h>

.section .rodata
fldcw_word: .word (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5) | (0b11 << 8)
ldmxcsr_dword: .long (1 << 7) | (1 << 8) | (1 << 9) | (1 << 10) | (1 << 11) | (1 << 12)

.section .text

.globl x86_64_context_switch
.globl x86_64_userspace_init_sysexit

// @brief Switches the context from the current thread to the next thread.
// @param rdi Pointer to the current thread's context structure.
// @param rsi Pointer to the next thread's context structure.
// @return The pointer to the (now) old thread's context structure
x86_64_context_switch:
    push rbx
    push rbp
    push r15
    push r14
    push r13
    push r12

    mov qword ptr [rdi + X86_64_THREAD_OFF_STACK_POINTER], rsp
    mov rsp, qword ptr [rsi + X86_64_THREAD_OFF_STACK_POINTER]

    xor r12, r12
    mov ds, r12
    mov es, r12

    pop r12
    pop r13
    pop r14
    pop r15
    pop rbp
    pop rbx

    mov rax, rdi
    ret

internal_userspace_exit_common:
    // return address of whatever called us
    // since we fuck with the stack we gotta grab this rq
    pop r10

    // load default fpu state
    fldcw word ptr [fldcw_word]
    ldmxcsr dword ptr [ldmxcsr_dword]

    pop rcx // address to sysret to
    pop rax // userspace stack pointer
    push r10 // push this back on the stack so ret can use it

    // @note: we don't bother clearing rbx, rbp, r12, r13, r14, r15
    // because they should be cleared by the context switch
    // we also don't clear rax because it's going to be used for sysret & fred
   	xor rdx, rdx
   	xor rsi, rsi
   	xor rdi, rdi
   	xor r8, r8
   	xor r9, r9
   	xor r10, r10

    mov r11, 0x202 // interrupts enabled
    ret

// void x86_64_userspace_init_sysexit();
.globl x86_64_userspace_init_sysexit
x86_64_userspace_init_sysexit:
    cli
    call internal_userspace_exit_common

    mov rsp, rax
    xor rax, rax

    swapgs
    sysretq

// void x86_64_userspace_init_sysexit();
.globl x86_64_userspace_fork_sysexit
x86_64_userspace_fork_sysexit:
    cli

    // setup registers
    // @note: we don't bother setting up rbx, rbp, r12, r13, r14, r15
    // because they should be setup by the context switch
    xor rax, rax
    xor rdx, rdx
   	pop rsi
   	pop rdi
   	pop r8
   	pop r9
   	pop r10

    // setup sysret
    pop rcx // address to sysret to
    pop rsp // userspace stack pointer
    mov r11, 0x202 // interrupts enabled

    swapgs
    sysretq
