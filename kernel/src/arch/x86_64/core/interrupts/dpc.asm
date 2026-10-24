section .text

global dpc_execute_on_stack

; @brief executes a DPC on the target stack
; @param fn (rdi): void* - dpc function pointer
; @param data (rsi): void* - dpc data
; @param stack (rdx): void* - target stack pointer
; @return void
dpc_execute_on_stack:
    push rbp
    mov rbp, rsp

    ; switch to the dpc stack
    mov rsp, rdx

    ; move fn into rax, and data into rdi
    mov rax, rdi
    mov rdi, rsi

    ; call fn
    call rax

    ; restore stack
    mov rsp, rbp
    pop rbp
    ret
