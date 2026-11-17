.intel_syntax noprefix
.section .text

.globl dpc_execute_on_stack
.type dpc_execute_on_stack, @function

/// @brief Executes a DPC on the target stack
/// @param rdi DPC function pointer
/// @param rsi DPC data
/// @param rdx Target stack pointer
/// @return void
dpc_execute_on_stack:
    push rbp
    mov rbp, rsp

    // switch to the dpc stack
    mov rsp, rdx

    // move fn into rax, and data into rdi
    mov rax, rdi
    mov rdi, rsi

    // call fn
    call rax

    // restore stack
    mov rsp, rbp
    pop rbp
    ret
.size dpc_execute_on_stack, . - dpc_execute_on_stack
