section .text
global x86_64_context_switch

; @brief Switches the context from the current thread to the next thread.
; @param rdi Pointer to the current thread's context structure.
; @param rsi Pointer to the next thread's context structure.
; @return The pointer to the (now) old thread's context structure
x86_64_context_switch:
    push rbx
    push rbp
    push r15
    push r14
    push r13
    push r12

    mov qword [rdi + 0], rsp
    mov rsp, qword [rsi + 0]

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
