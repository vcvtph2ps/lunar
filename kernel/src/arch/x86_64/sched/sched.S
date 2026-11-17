#include <arch/x86_64/internal/asm_offsets.h>

.intel_syntax noprefix

.section .rodata
.balign 8
fldcw_word:    .word (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5) | (0b11 << 8)
ldmxcsr_dword: .long (1 << 7) | (1 << 8) | (1 << 9) | (1 << 10) | (1 << 11) | (1 << 12)

.section .text

.globl x86_64_context_switch
.type x86_64_context_switch, @function

/// @brief Switches the context from the current thread to the next thread.
/// @param rdi Pointer to the current thread's context structure.
/// @param rsi Pointer to the next thread's context structure.
/// @return The pointer to the (now) old thread's context structure
x86_64_context_switch:
    // save the callee-saved registers of the outgoing thread
    push rbx
    push rbp
    push r15
    push r14
    push r13
    push r12

    // swap stacks
    mov qword ptr [rdi + X86_64_THREAD_OFF_STACK_POINTER], rsp
    mov rsp, qword ptr [rsi + X86_64_THREAD_OFF_STACK_POINTER]

    // clear the user data segments so they get reloaded on the next trip to userspace
    xor r12, r12
    mov ds, r12
    mov es, r12

    // restore the callee-saved registers of the incoming thread
    pop r12
    pop r13
    pop r14
    pop r15
    pop rbp
    pop rbx

    mov rax, rdi
    ret
.size x86_64_context_switch, . - x86_64_context_switch

.globl internal_userspace_exit_common
.type internal_userspace_exit_common, @function

/// @brief Common code shared by the userspace entry stubs
/// @note Expects a return address, then rcx (user rip) and rax (user rsp) on the stack
internal_userspace_exit_common:
    // grab the return address since we need rcx and rax
    pop r10

    // load the default x87 and SSE control words
    fldcw word ptr [fldcw_word]
    ldmxcsr dword ptr [ldmxcsr_dword]

    pop rcx // address to sysret to
    pop rax // userspace stack pointer
    push r10 // put the return address back so ret can use it

    // @note rbx, rbp and r12-r15 are left alone as the context switch clears them,
    // and rax is left alone as the stack pointer
    xor rdx, rdx
    xor rsi, rsi
    xor rdi, rdi
    xor r8, r8
    xor r9, r9
    xor r10, r10

    mov r11, 0x202 // interrupts enabled
    ret
.size internal_userspace_exit_common, . - internal_userspace_exit_common

.globl x86_64_userspace_init_sysexit
.type x86_64_userspace_init_sysexit, @function

/// @brief entry into userspace for a freshly created thread
/// @return Does not return
x86_64_userspace_init_sysexit:
    cli
    call internal_userspace_exit_common

    mov rsp, rax
    xor rax, rax

    swapgs
    sysretq
.size x86_64_userspace_init_sysexit, . - x86_64_userspace_init_sysexit

.globl x86_64_userspace_fork_sysexit
.type x86_64_userspace_fork_sysexit, @function

/// @brief entry into userspace for a forked thread
/// @return Does not return.
x86_64_userspace_fork_sysexit:
    cli

    // @note rbx, rbp and r12-r15 are left alone as the context switch restores them
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
.size x86_64_userspace_fork_sysexit, . - x86_64_userspace_fork_sysexit
